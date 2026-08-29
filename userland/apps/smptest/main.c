#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/system.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define SMPTEST_DEFAULT_ROUNDS 8000000ULL
#define SMPTEST_MAX_ROUNDS 200000000ULL
#define SMPTEST_SAMPLE_INTERVAL 262144ULL

struct worker_record {
    pid_t pid;
    uint32_t cpu;
    uint64_t mask;
};

static uint32_t parse_worker_count(const char *text, uint32_t fallback,
                                   uint32_t maximum)
{
    char *end = 0;
    unsigned long value;
    if (!text || !text[0]) {
        return fallback;
    }
    value = strtoul(text, &end, 10);
    if (end == text || *end || value == 0 || value > maximum) {
        return fallback;
    }
    return (uint32_t)value;
}

static uint64_t parse_rounds(const char *text)
{
    char *end = 0;
    unsigned long value;
    if (!text || !text[0]) {
        return SMPTEST_DEFAULT_ROUNDS;
    }
    value = strtoul(text, &end, 10);
    if (end == text || *end || value == 0) {
        return SMPTEST_DEFAULT_ROUNDS;
    }
    if ((uint64_t)value > SMPTEST_MAX_ROUNDS) {
        return SMPTEST_MAX_ROUNDS;
    }
    return (uint64_t)value;
}

static int sample_target_cpu(uint32_t target_cpu, pid_t pid, int *observed)
{
    struct leonos_perf_info info = {0};
    int ret = leonos_perf_info(&info);
    if (ret < 0) {
        return ret;
    }
    if (target_cpu < info.cpu_count &&
        info.cpus[target_cpu].online &&
        info.cpus[target_cpu].current_pid == (uint32_t)pid) {
        *observed = 1;
    }
    return 0;
}

static int run_worker(uint32_t worker, uint32_t target_cpu, uint64_t mask,
                      uint64_t rounds)
{
    uint64_t actual_mask = 0;
    uint64_t checksum = 0x9e3779b97f4a7c15ULL ^ ((uint64_t)worker << 32);
    pid_t pid = getpid();
    int observed = 0;
    int ret;

    ret = leonos_task_affinity_set(0, mask);
    if (ret < 0) {
        printf("[smptest.elf] worker=%u pid=%d affinity set failed ret=%d\n",
               worker, pid, ret);
        return 10;
    }
    ret = leonos_task_affinity_get(0, &actual_mask);
    if (ret < 0 || actual_mask != mask) {
        printf("[smptest.elf] worker=%u pid=%d affinity mismatch wanted=0x%lx got=0x%lx ret=%d\n",
               worker, pid, (unsigned long)mask, (unsigned long)actual_mask, ret);
        return 11;
    }

    for (uint64_t i = 0; i < rounds; ++i) {
        checksum ^= i + ((uint64_t)worker << 17);
        checksum = checksum * 6364136223846793005ULL + 1442695040888963407ULL;
        checksum ^= checksum >> 29;
        if ((i & (SMPTEST_SAMPLE_INTERVAL - 1ULL)) == 0) {
            ret = sample_target_cpu(target_cpu, pid, &observed);
            if (ret < 0) {
                return 12;
            }
            (void)sched_yield();
        }
    }
    ret = sample_target_cpu(target_cpu, pid, &observed);
    if (ret < 0) {
        return 12;
    }
    printf("[smptest.elf] worker=%u pid=%d cpu=%u mask=0x%lx checksum=0x%lx observed=%d\n",
           worker, pid, target_cpu, (unsigned long)mask,
           (unsigned long)checksum, observed);
    if (!observed) {
        return 13;
    }
    return 0;
}

static void print_cpu_snapshot(const char *label,
                               const struct leonos_perf_info *info)
{
    printf("[smptest.elf] %s cpus=%u online=%u tasks=%u running=%u ready=%u sleeping=%u\n",
           label, info->cpu_count, info->online_cpu_count, info->task_count,
           info->running_tasks, info->ready_tasks, info->sleeping_tasks);
    for (uint32_t cpu = 0; cpu < info->cpu_count; ++cpu) {
        if (!info->cpus[cpu].online) {
            continue;
        }
        printf("[smptest.elf] %s cpu%u apic=%u busy=%lu idle=%lu current=%u rq=%u\n",
               label, cpu, info->cpus[cpu].apic_id,
               (unsigned long)info->cpus[cpu].busy_ticks,
               (unsigned long)info->cpus[cpu].idle_ticks,
               info->cpus[cpu].current_pid,
               info->cpus[cpu].runqueue_length);
    }
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_perf_info before = {0};
    struct leonos_perf_info after = {0};
    struct worker_record workers[LEONOS_PERF_MAX_CPUS];
    uint32_t target_cpus[LEONOS_PERF_MAX_CPUS];
    uint32_t online = 0;
    uint32_t worker_count;
    uint32_t created = 0;
    uint32_t failures = 0;
    uint64_t rounds;
    int ret;

    (void)envp;
    ret = leonos_perf_info(&before);
    if (ret < 0) {
        printf("[smptest.elf] FAIL perf_info ret=%d\n", ret);
        return 1;
    }
    for (uint32_t cpu = 0; cpu < before.cpu_count &&
                            cpu < LEONOS_PERF_MAX_CPUS; ++cpu) {
        if (before.cpus[cpu].online) {
            target_cpus[online++] = cpu;
        }
    }
    if (online == 0) {
        puts("[smptest.elf] FAIL no online CPUs");
        return 1;
    }
    worker_count = parse_worker_count(argc > 1 ? argv[1] : 0, online, online);
    rounds = parse_rounds(argc > 2 ? argv[2] : 0);
    print_cpu_snapshot("before", &before);
    printf("[smptest.elf] starting workers=%u rounds=%lu\n",
           worker_count, (unsigned long)rounds);

    for (uint32_t worker = 0; worker < worker_count; ++worker) {
        uint32_t cpu = target_cpus[worker];
        uint64_t mask = 1ULL << cpu;
        pid_t pid = fork();
        if (pid < 0) {
            printf("[smptest.elf] fork worker=%u failed ret=%d\n", worker, pid);
            ++failures;
            continue;
        }
        if (pid == 0) {
            exit(run_worker(worker, cpu, mask, rounds));
        }
        workers[created++] = (struct worker_record){.pid = pid, .cpu = cpu, .mask = mask};
    }

    for (uint32_t i = 0; i < created; ++i) {
        int status = 0;
        pid_t waited = waitpid(workers[i].pid, &status, 0);
        if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            printf("[smptest.elf] FAIL worker pid=%d cpu=%u status=%d wait=%d\n",
                   workers[i].pid, workers[i].cpu, code, waited);
            ++failures;
        }
    }

    ret = leonos_perf_info(&after);
    if (ret < 0) {
        printf("[smptest.elf] FAIL final perf_info ret=%d\n", ret);
        return 1;
    }
    print_cpu_snapshot("after", &after);
    for (uint32_t i = 0; i < created; ++i) {
        uint32_t cpu = workers[i].cpu;
        uint64_t busy_before = cpu < before.cpu_count ? before.cpus[cpu].busy_ticks : 0;
        uint64_t busy_after = cpu < after.cpu_count ? after.cpus[cpu].busy_ticks : 0;
        if (busy_after <= busy_before) {
            printf("[smptest.elf] warning cpu%u busy ticks did not advance (%lu -> %lu)\n",
                   cpu, (unsigned long)busy_before, (unsigned long)busy_after);
        }
    }
    if (created != worker_count) {
        ++failures;
    }
    if (failures) {
        printf("[smptest.elf] FAIL workers=%u/%u failures=%u\n",
               created, worker_count, failures);
        return 1;
    }
    if (online < 2) {
        puts("[smptest.elf] PASS single-CPU baseline (SMP unavailable)");
    } else {
        printf("[smptest.elf] PASS SMP workers=%u online_cpus=%u\n",
               worker_count, online);
    }
    return 0;
}
