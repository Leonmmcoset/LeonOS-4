static void ahci_cpu_relax(void)
{
    __asm__ volatile("pause");
}

static void ahci_memory_barrier(void)
{
    /* The command list and table live in normal RAM while PxCI is MMIO.
     * Keep descriptor stores on the visible side of command submission and
     * do not consume DMA output before the controller has cleared PxCI. */
    __asm__ volatile("mfence" ::: "memory");
}

static int ahci_async_fast_poll_idle(struct ahci_hba_port *port)
{
    for (uint32_t i = 0; i < AHCI_ASYNC_FAST_POLL_SPINS; ++i) {
        if ((port->tfd & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0) {
            return 0;
        }
        ahci_cpu_relax();
    }
    return -LEONOS_EAGAIN;
}

static int ahci_async_fast_poll_command(struct ahci_hba_port *port)
{
    for (uint32_t i = 0; i < AHCI_ASYNC_FAST_POLL_SPINS; ++i) {
        if ((port->ci & 1u) == 0) {
            return 0;
        }
        ahci_cpu_relax();
    }
    return -LEONOS_EAGAIN;
}

static int ahci_wait_idle(struct ahci_hba_port *port)
{
    if (storage_async_can_yield() &&
        (port->tfd & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) != 0) {
        return ahci_async_fast_poll_idle(port);
    }
    for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
        if ((port->tfd & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0) {
            return 0;
        }
        ahci_cpu_relax();
    }
    return -1;
}

static int ahci_wait_cmd_slot(struct ahci_hba_port *port)
{
    if (storage_async_can_yield() && (port->ci | port->sact) != 0) {
        for (uint32_t i = 0; i < AHCI_ASYNC_FAST_POLL_SPINS; ++i) {
            if ((port->ci | port->sact) == 0) {
                return 0;
            }
            ahci_cpu_relax();
        }
        return -LEONOS_EAGAIN;
    }
    for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
        if ((port->ci | port->sact) == 0) {
            return 0;
        }
        ahci_cpu_relax();
    }
    return -5;
}

static void ahci_pending_clear(void)
{
    storage_memzero(&ahci_pending_command, sizeof(ahci_pending_command));
}

static void ahci_log_pending_failure(const char *reason,
                                     struct ahci_hba_port *port)
{
    if (!port) {
        return;
    }
    console_printf("[ntclks] ahci %s op=%s owner=%u lba=%llu sectors=%u "
                   "ci=0x%x is=0x%x tfd=0x%x serr=0x%x\n",
                   reason,
                   ahci_pending_command.write ? "write" : "read",
                   ahci_pending_command.owner_pid,
                   (unsigned long long)ahci_pending_command.lba,
                   ahci_pending_command.sector_count,
                   port->ci,
                   port->is,
                   port->tfd,
                   port->serr);
}

static int ahci_pending_poll(void)
{
    struct ahci_hba_port *port = ahci_pending_command.port;
    if (!ahci_pending_command.active || !port) {
        return -22;
    }
    if ((port->ci & 1u) != 0) {
        if (storage_async_can_yield()) {
            int poll_ret = ahci_async_fast_poll_command(port);
            if (poll_ret == 0) {
                goto command_complete;
            }
            if (time_ticks() - ahci_pending_command.start_tick >=
                AHCI_ASYNC_TIMEOUT_TICKS) {
                /* PxCI is cleared by the controller, not software.  Do not
                 * overwrite it here: the retry path below will stop and
                 * reinitialise the port before reusing command memory. */
                ahci_log_pending_failure("timeout", port);
                ahci_pending_clear();
                return -5;
            }
            return -LEONOS_EAGAIN;
        }
        for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
            if ((port->ci & 1u) == 0) {
                break;
            }
            ahci_cpu_relax();
        }
        if ((port->ci & 1u) != 0) {
            ahci_log_pending_failure("wait timeout", port);
            ahci_pending_clear();
            return -5;
        }
    }
command_complete:
    if (port->is & AHCI_PORT_IS_TFES) {
        ahci_log_pending_failure("task-file error", port);
        ahci_pending_clear();
        return -5;
    }
    ahci_memory_barrier();
    ahci_pending_clear();
    return 0;
}

static int ahci_pending_matches(struct ahci_hba_port *port, uint64_t lba,
                                uint32_t sector_count, const void *buffer,
                                uint8_t write)
{
    return ahci_pending_command.active &&
           ahci_pending_command.port == port &&
           ahci_pending_command.lba == lba &&
           ahci_pending_command.sector_count == sector_count &&
           ahci_pending_command.buffer == buffer &&
           ahci_pending_command.write == write;
}

void storage_drain_task_io(uint32_t pid)
{
    bool saved_async;
    bool saved_write_started;
    if (!pid) {
        return;
    }
    if (!ahci_pending_command.active ||
        ahci_pending_command.owner_pid != pid) {
        storage_release_task_io(pid);
        return;
    }
    /* The task's address space is about to be released.  Finish its DMA
     * before pages can be reused by another process.  This runs only while
     * reaping an exited owner, never on the normal application I/O path. */
    saved_async = storage_io_async_context;
    saved_write_started = storage_io_write_started;
    storage_io_async_context = false;
    storage_io_write_started = true;
    (void)ahci_pending_poll();
    storage_io_async_context = saved_async;
    storage_io_write_started = saved_write_started;
    storage_release_task_io(pid);
}

static int storage_read_failure(int ret)
{
    return ret == -LEONOS_EAGAIN ? ret : -5;
}

static int ahci_stop_port(struct ahci_hba_port *port)
{
    if (!port) {
        return -22;
    }
    port->cmd &= ~AHCI_PORT_CMD_ST;
    port->cmd &= ~AHCI_PORT_CMD_FRE;
    for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
        if ((port->cmd & (AHCI_PORT_CMD_FR | AHCI_PORT_CMD_CR)) == 0) {
            return 0;
        }
        ahci_cpu_relax();
    }
    return -5;
}

static void ahci_start_port(struct ahci_hba_port *port)
{
    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST;
}

static int ahci_setup_port(struct ahci_hba_port *port)
{
    if (ahci_stop_port(port) < 0) {
        return -5;
    }
    storage_memzero(ahci_cmd_headers, sizeof(ahci_cmd_headers));
    storage_memzero(ahci_received_fis, sizeof(ahci_received_fis));
    storage_memzero(ahci_cmd_table_buf, sizeof(ahci_cmd_table_buf));

    port->clb = (uint32_t)(uintptr_t)ahci_cmd_headers;
    port->clbu = 0;
    port->fb = (uint32_t)(uintptr_t)ahci_received_fis;
    port->fbu = 0;

    struct ahci_cmd_header *cmd = &ahci_cmd_headers[0];
    cmd->prdtl = AHCI_CMDH_PRDTL;
    cmd->ctba = (uint32_t)(uintptr_t)ahci_cmd_table_buf;
    cmd->ctbau = 0;

    port->is = 0xffffffffu;
    port->serr = 0xffffffffu;
    ahci_memory_barrier();
    ahci_start_port(port);
    return 0;
}

static int ahci_recover_port(struct ahci_hba_port *port, uint32_t attempt)
{
    if (!port) {
        return -22;
    }
    console_printf("[ntclks] ahci recovering port after I/O failure attempt=%u "
                   "ci=0x%x is=0x%x tfd=0x%x serr=0x%x\n",
                   attempt, port->ci, port->is, port->tfd, port->serr);
    ahci_pending_clear();
    if (ahci_setup_port(port) < 0) {
        console_printf("[ntclks] ahci port recovery failed attempt=%u\n", attempt);
        return -5;
    }
    return 0;
}

static int ahci_read_lba(struct ahci_hba_port *port, uint64_t lba, uint32_t sector_count, void *buffer)
{
    struct ahci_cmd_header *hdr;
    struct ahci_cmd_table *tbl;
    struct fis_reg_h2d *fis;
    int pending_ret;
    int pending_matches;

    if (!port || !buffer || sector_count == 0 || sector_count > AHCI_MAX_SECTORS) {
        return -22;
    }
    if (ahci_pending_command.active) {
        pending_matches = ahci_pending_matches(port, lba, sector_count, buffer, 0);
        pending_ret = ahci_pending_poll();
        if (pending_matches || pending_ret < 0) {
            return pending_ret;
        }
        if (pending_ret != 0) {
            return -5;
        }
    }
    if (ahci_wait_idle(port) < 0) {
        return storage_async_can_yield() ? -LEONOS_EAGAIN : -5;
    }

    port->is = 0xffffffffu;
    hdr = &ahci_cmd_headers[0];
    hdr->flags = (uint16_t)((sizeof(struct fis_reg_h2d) / sizeof(uint32_t)) & 0x1f);
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    tbl = (struct ahci_cmd_table *)(void *)ahci_cmd_table_buf;
    storage_memzero(tbl, sizeof(ahci_cmd_table_buf));
    tbl->prdt[0].dba = (uint32_t)(uintptr_t)buffer;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc = (sector_count * SECTOR_SIZE) - 1u;

    fis = (struct fis_reg_h2d *)(void *)tbl->cfis;
    storage_memzero(fis, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_READ_DMA_EXT;
    fis->device = 1u << 6;
    fis->lba0 = (uint8_t)(lba & 0xffu);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xffu);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xffu);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xffu);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xffu);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xffu);
    fis->countl = (uint8_t)(sector_count & 0xffu);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xffu);

    if (ahci_wait_cmd_slot(port) < 0) {
        return storage_async_can_yield() ? -LEONOS_EAGAIN : -5;
    }
    ahci_memory_barrier();
    port->ci = 1u;
    ahci_pending_command.port = port;
    ahci_pending_command.lba = lba;
    ahci_pending_command.sector_count = sector_count;
    ahci_pending_command.buffer = buffer;
    ahci_pending_command.start_tick = time_ticks();
    ahci_pending_command.owner_pid = sched_current_pid();
    ahci_pending_command.write = 0;
    ahci_pending_command.active = 1;
    return ahci_pending_poll();
}

static int ahci_read_atapi_blocks(struct ahci_hba_port *port, uint64_t lba,
                                  uint32_t block_count, void *buffer)
{
    struct ahci_cmd_header *hdr;
    struct ahci_cmd_table *tbl;
    struct fis_reg_h2d *fis;
    uint8_t *packet;
    int pending_ret;
    int pending_matches;

    if (!port || !buffer || block_count == 0 || block_count > 32u) {
        return -22;
    }
    if (ahci_pending_command.active) {
        pending_matches = ahci_pending_matches(port, lba, block_count, buffer, 0);
        pending_ret = ahci_pending_poll();
        if (pending_matches || pending_ret < 0) {
            return pending_ret;
        }
        if (pending_ret != 0) {
            return -5;
        }
    }
    if (ahci_wait_idle(port) < 0) {
        return storage_async_can_yield() ? -LEONOS_EAGAIN : -5;
    }

    port->is = 0xffffffffu;
    hdr = &ahci_cmd_headers[0];
    /* ATAPI PACKET has a five-dword command FIS and transfers data to RAM. */
    /* CFL=5 and ATAPI command. READ(10) transfers from the device, so W is clear. */
    hdr->flags = (uint16_t)((5u & 0x1fu) | (1u << 5));
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    tbl = (struct ahci_cmd_table *)(void *)ahci_cmd_table_buf;
    storage_memzero(tbl, sizeof(ahci_cmd_table_buf));
    tbl->prdt[0].dba = (uint32_t)(uintptr_t)buffer;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc = (block_count * ISO9660_BLOCK_SIZE) - 1u;

    fis = (struct fis_reg_h2d *)(void *)tbl->cfis;
    storage_memzero(fis, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_PACKET;
    /* Set the ATAPI DMA feature bit. */
    fis->featurel = 1u;

    packet = tbl->acmd;
    packet[0] = SCSI_CMD_READ10;
    packet[2] = (uint8_t)((lba >> 24) & 0xffu);
    packet[3] = (uint8_t)((lba >> 16) & 0xffu);
    packet[4] = (uint8_t)((lba >> 8) & 0xffu);
    packet[5] = (uint8_t)(lba & 0xffu);
    packet[7] = (uint8_t)((block_count >> 8) & 0xffu);
    packet[8] = (uint8_t)(block_count & 0xffu);

    if (ahci_wait_cmd_slot(port) < 0) {
        return storage_async_can_yield() ? -LEONOS_EAGAIN : -5;
    }
    ahci_memory_barrier();
    port->ci = 1u;
    ahci_pending_command.port = port;
    ahci_pending_command.lba = lba;
    ahci_pending_command.sector_count = block_count;
    ahci_pending_command.buffer = buffer;
    ahci_pending_command.start_tick = time_ticks();
    ahci_pending_command.owner_pid = sched_current_pid();
    ahci_pending_command.write = 0;
    ahci_pending_command.active = 1;
    return ahci_pending_poll();
}

static int ahci_read_atapi_blocks_retry(struct ahci_hba_port *port, uint64_t lba,
                                        uint32_t block_count, void *buffer)
{
    int ret = -5;
    for (uint32_t attempt = 0; attempt < AHCI_IO_RETRY_COUNT; ++attempt) {
        ret = ahci_read_atapi_blocks(port, lba, block_count, buffer);
        if (ret >= 0 || ret == -LEONOS_EAGAIN) {
            return ret;
        }
        if (ahci_recover_port(port, attempt + 1u) < 0) {
            break;
        }
        if (attempt + 1u >= AHCI_IO_RETRY_COUNT) {
            break;
        }
    }
    return ret;
}

static int ahci_write_lba(struct ahci_hba_port *port, uint64_t lba, uint32_t sector_count, const void *buffer)
{
    struct ahci_cmd_header *hdr;
    struct ahci_cmd_table *tbl;
    struct fis_reg_h2d *fis;
    int pending_ret;
    int pending_matches;

    if (!port || !buffer || sector_count == 0 || sector_count > AHCI_MAX_SECTORS) {
        return -22;
    }
    if (ahci_pending_command.active) {
        pending_matches = ahci_pending_matches(port, lba, sector_count, buffer, 1);
        pending_ret = ahci_pending_poll();
        if (pending_matches || pending_ret < 0) {
            return pending_ret;
        }
        if (pending_ret != 0) {
            return -5;
        }
    }
    if (ahci_wait_idle(port) < 0) {
        return storage_async_can_yield() ? -LEONOS_EAGAIN : -5;
    }

    port->is = 0xffffffffu;
    hdr = &ahci_cmd_headers[0];
    hdr->flags = (uint16_t)(((sizeof(struct fis_reg_h2d) / sizeof(uint32_t)) & 0x1f) | (1u << 6));
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    tbl = (struct ahci_cmd_table *)(void *)ahci_cmd_table_buf;
    storage_memzero(tbl, sizeof(ahci_cmd_table_buf));
    tbl->prdt[0].dba = (uint32_t)(uintptr_t)buffer;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc = (sector_count * SECTOR_SIZE) - 1u;

    fis = (struct fis_reg_h2d *)(void *)tbl->cfis;
    storage_memzero(fis, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_WRITE_DMA_EXT;
    fis->device = 1u << 6;
    fis->lba0 = (uint8_t)(lba & 0xffu);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xffu);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xffu);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xffu);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xffu);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xffu);
    fis->countl = (uint8_t)(sector_count & 0xffu);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xffu);

    if (ahci_wait_cmd_slot(port) < 0) {
        return storage_async_can_yield() ? -LEONOS_EAGAIN : -5;
    }
    ahci_memory_barrier();
    port->ci = 1u;
    ahci_pending_command.port = port;
    ahci_pending_command.lba = lba;
    ahci_pending_command.sector_count = sector_count;
    ahci_pending_command.buffer = (void *)buffer;
    ahci_pending_command.start_tick = time_ticks();
    ahci_pending_command.owner_pid = sched_current_pid();
    ahci_pending_command.write = 1;
    ahci_pending_command.active = 1;
    return ahci_pending_poll();
}

static int ahci_read_lba_retry(struct ahci_hba_port *port, uint64_t lba,
                               uint32_t sector_count, void *buffer)
{
    int ret = -5;
    for (uint32_t attempt = 0; attempt < AHCI_IO_RETRY_COUNT; ++attempt) {
        ret = ahci_read_lba(port, lba, sector_count, buffer);
        if (ret >= 0 || ret == -LEONOS_EAGAIN) {
            return ret;
        }
        if (ahci_recover_port(port, attempt + 1u) < 0) {
            break;
        }
        if (attempt + 1u >= AHCI_IO_RETRY_COUNT) {
            break;
        }
    }
    return ret;
}

static int ahci_write_lba_retry(struct ahci_hba_port *port, uint64_t lba,
                                uint32_t sector_count, const void *buffer)
{
    int ret = -5;
    for (uint32_t attempt = 0; attempt < AHCI_IO_RETRY_COUNT; ++attempt) {
        ret = ahci_write_lba(port, lba, sector_count, buffer);
        if (ret >= 0 || ret == -LEONOS_EAGAIN) {
            return ret;
        }
        /* Writes are retried at the same sector range with unchanged data.
         * That is safe even when the controller completed a command just as
         * the timeout/error status was observed. */
        if (ahci_recover_port(port, attempt + 1u) < 0) {
            break;
        }
        if (attempt + 1u >= AHCI_IO_RETRY_COUNT) {
            break;
        }
    }
    return ret;
}

