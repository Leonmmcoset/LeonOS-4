#include <ntclks/inventory.h>
#include <ntclks/smp.h>
#include <ntclks/text_stream.h>

static struct cpu_inventory inventory[SMP_MAX_CPUS];
/** @brief Read CPUID leaf/subleaf on this CPU; only used during CPU bring-up. */
static void cpu_leaf(uint32_t leaf, uint32_t sub, uint32_t out[4])
{
    __asm__ volatile("cpuid"
                     : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
                     : "a"(leaf), "c"(sub));
}
/** @brief Capture immutable hardware facts before the CPU becomes visible online. */
void cpu_inventory_capture(uint32_t cpu)
{
    if (cpu >= SMP_MAX_CPUS)
        return;
    struct cpu_inventory value = {0};
    uint32_t r[4], max, ext;
    cpu_leaf(0, 0, r);
    max = r[0];
    __builtin_memcpy(value.vendor, r + 1, 4);
    __builtin_memcpy(value.vendor + 4, r + 3, 4);
    __builtin_memcpy(value.vendor + 8, r + 2, 4);
    cpu_leaf(1, 0, r);
    value.family = (r[0] >> 8) & 15;
    value.model = (r[0] >> 4) & 15;
    value.stepping = r[0] & 15;
    if (value.family == 6 || value.family == 15)
        value.model |= ((r[0] >> 16) & 15) << 4;
    if (value.family == 15)
        value.family += (r[0] >> 20) & 255;
    value.apic_id = r[1] >> 24;
    value.leaf1_edx = r[3];
    value.leaf1_ecx = r[2];
    uint32_t logical = (r[1] >> 16) & 255, core_count = 1, smt_shift = 0, package_shift = 0;
    uint32_t topology = max >= 0x1f ? 0x1f : max >= 0xb ? 0xb : 0;
    if (topology) {
        cpu_leaf(topology, 0, r);
        if (!r[1] && topology == 0x1f && max >= 0xb) {
            topology = 0xb;
            cpu_leaf(topology, 0, r);
        }
        if (!r[1])
            topology = 0;
    }
    if (topology) {
        for (uint32_t sub = 0; sub < 32; ++sub) {
            cpu_leaf(topology, sub, r);
            uint32_t type = (r[2] >> 8) & 255;
            if (!r[1] || !type)
                break;
            value.apic_id = r[3];
            if (type == 1)
                smt_shift = r[0] & 31;
            if ((r[0] & 31) > package_shift)
                package_shift = r[0] & 31;
        }
    } else {
        if (max >= 4) {
            cpu_leaf(4, 0, r);
            if (r[0] & 31)
                core_count = (r[0] >> 26) + 1;
        }
        uint32_t threads = logical > core_count ? logical / core_count : 1;
        while ((1u << smt_shift) < threads && smt_shift < 31)
            ++smt_shift;
        while ((1u << package_shift) < logical && package_shift < 31)
            ++package_shift;
    }
    value.package_id = value.apic_id >> package_shift;
    value.core_id = (value.apic_id & ((1u << package_shift) - 1)) >> smt_shift;
    cpu_leaf(0x80000000, 0, r);
    ext = r[0];
    if (ext >= 0x80000004)
        for (uint32_t i = 0; i < 3; ++i) {
            cpu_leaf(0x80000002 + i, 0, r);
            __builtin_memcpy(value.model_name + i * 16, r, 16);
        }
    if (!topology && ext >= 0x8000001e && !__builtin_strcmp(value.vendor, "AuthenticAMD")) {
        cpu_leaf(0x80000008, 0, r);
        uint32_t bits = (r[2] >> 12) & 15;
        if (!bits) {
            uint32_t cores = (r[2] & 255) + 1;
            while ((1u << bits) < cores)
                ++bits;
        }
        cpu_leaf(0x8000001e, 0, r);
        value.apic_id = r[0];
        value.core_id = r[1] & 255;
        value.package_id = r[0] >> bits;
    }
    if (max >= 0x16) {
        cpu_leaf(0x16, 0, r);
        value.base_mhz = r[0] & 65535;
        value.max_mhz = r[1] & 65535;
    }
    value.valid = true;
    inventory[cpu] = value;
}
/** @brief Return a captured CPU record; uncaptured/off-range CPUs have no record. */
const struct cpu_inventory *cpu_inventory_get(uint32_t cpu)
{
    return cpu < SMP_MAX_CPUS && inventory[cpu].valid ? &inventory[cpu] : 0;
}
/** @brief Emit a Linux cpuinfo decimal field. */
static void cpu_field(struct text_stream *s, const char *name, uint64_t value)
{
    text_string(s, name);
    text_string(s, "\t: ");
    text_unsigned(s, value);
    text_string(s, "\n");
}
/** @brief Read real online CPU records with offset/short-read handling. */
int cpu_inventory_read(uint64_t offset, void *buffer, uint32_t capacity, uint32_t *out_read)
{
    struct text_stream s = {.offset = offset, .buffer = buffer, .capacity = capacity};
    if (out_read)
        *out_read = 0;
    if (!buffer && capacity)
        return -22;
    for (uint32_t i = 0; i < smp_cpu_count(); ++i) {
        const struct cpu_inventory *c = cpu_inventory_get(i);
        if (!c || !smp_cpu_online(i))
            continue;
        uint32_t siblings = 0, cores = 0;
        for (uint32_t j = 0; j < smp_cpu_count(); ++j) {
            const struct cpu_inventory *d = cpu_inventory_get(j);
            if (!d || !smp_cpu_online(j) || c->package_id != d->package_id)
                continue;
            ++siblings;
            bool unique = true;
            for (uint32_t k = 0; k < j; ++k) {
                const struct cpu_inventory *e = cpu_inventory_get(k);
                if (e && smp_cpu_online(k) && e->package_id == d->package_id && e->core_id == d->core_id)
                    unique = false;
            }
            cores += unique;
        }
        cpu_field(&s, "processor", i);
        text_string(&s, "vendor_id\t: ");
        text_string(&s, c->vendor);
        text_string(&s, "\n");
        cpu_field(&s, "cpu family", c->family);
        cpu_field(&s, "model", c->model);
        text_string(&s, "model name\t: ");
        text_string(&s, c->model_name);
        text_string(&s, "\n");
        cpu_field(&s, "stepping", c->stepping);
        /* CPUID.16 is nominal frequency, not a sampled instantaneous clock. */
        cpu_field(&s, "physical id", c->package_id);
        cpu_field(&s, "siblings", siblings);
        cpu_field(&s, "core id", c->core_id);
        cpu_field(&s, "cpu cores", cores);
        cpu_field(&s, "apicid", c->apic_id);
        text_string(&s, "fpu\t\t: ");
        text_string(&s, (c->leaf1_edx & 1) ? "yes\n" : "no\n");
        text_string(&s, "flags\t\t:");
        static const struct {
            uint32_t bit;
            const char *name;
        } flags[] = {{0, " fpu"},  {4, " tsc"},   {5, " msr"},  {8, " cx8"},  {15, " cmov"},
                     {23, " mmx"}, {24, " fxsr"}, {25, " sse"}, {26, " sse2"}};
        for (uint32_t f = 0; f < sizeof(flags) / sizeof(flags[0]); ++f)
            if (c->leaf1_edx & (1u << flags[f].bit))
                text_string(&s, flags[f].name);
        text_string(&s, "\n\n");
    }
    if (out_read)
        *out_read = s.written;
    return 0;
}
