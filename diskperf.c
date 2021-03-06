// -*- c-basic-offset: 4; c-backslash-column: 79; indent-tabs-mode: nil -*-
// vim:sw=4 ts=4 sts=4 expandtab
/* Copyright 2013, SecurActive.
 *
 * Diskperf is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Diskperf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Diskperf.  If not, see <http://www.gnu.org/licenses/>.
 */
#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <getopt.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

static bool drop_caches = false;
static size_t block_size = 4096;    // how many bytes to read at each read()
static unsigned nb_blocks = 1000;    // number of blocks to read from each devs
static bool sequential = false;     // whether to read block sequentially rather than at random
static bool noatime = false;
static bool verbose = false;

static void do_drop_caches(void)
{
    errno = 0;
    int status = system("sysctl -q vm.drop_caches=1");

    if (status == -1) {
        fprintf(stderr, "system(sysctl): %s\n", errno ? strerror(errno) : "failed");
        return;
    }

    if (WIFEXITED(status) && 0 != WEXITSTATUS(status)) {
        fprintf(stderr, "sysctl: returned %d\n", WEXITSTATUS(status));
        return;
    }
}

static void do_time_dev(char const *fname)
{
    char buf[block_size];

    if (drop_caches) do_drop_caches();

    int fd = open(fname, O_RDONLY | (noatime ? O_NOATIME:0));
    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", fname, strerror(errno));
        return;
    }
    (void)posix_fadvise(fd, 0, 0, POSIX_FADV_NOREUSE);  // best effort

    // Get file size
    off_t size;
    struct stat stats;
    if (stat(fname, &stats) < 0) {
        fprintf(stderr, "stat(%s): %s\n", fname, strerror(errno));
        goto quit;
    }
    if (S_ISBLK(stats.st_mode)) {
        unsigned long dev_blk_sz;
        if (ioctl(fd, BLKGETSIZE, &dev_blk_sz) < 0) {
            fprintf(stderr, "ioctl(%s): %s\n", fname, strerror(errno));
            goto quit;
        }
        size = dev_blk_sz * 512;
    } else {
        size = stats.st_size;
    }

    assert(size > 0);
    if (size < (off_t)block_size) {
        fprintf(stderr, "File %s too small (%lld)\n", fname, (unsigned long long)size);
        goto quit;
    }

    struct timeval start;
    gettimeofday(&start, NULL);
#   define WRAP_SIZE(o) ((off_t)((o) % (size - block_size)))
#   define RANDOM_OFFSET() ((off_t)(WRAP_SIZE(rand()) / block_size) * (off_t)block_size)
    off_t seq_pos = RANDOM_OFFSET(); // used only for sequential reads
    unsigned b;
    for (b = 0; b < nb_blocks; b++, seq_pos = WRAP_SIZE(seq_pos + block_size)) {
        off_t pos = sequential ? seq_pos : RANDOM_OFFSET();
        if ((off_t)-1 == lseek(fd, pos, SEEK_SET)) {
            fprintf(stderr, "lseek(%s): %s\n", fname, strerror(errno));
            break;
        }

        ssize_t ret = read(fd, buf, block_size);
        if (ret < 0) {
            fprintf(stderr, "read(%s): %s\n", fname, strerror(errno));
            break;
        }

        if ((size_t)ret != block_size) {
            fprintf(stderr, "read(%s): short read at %lld!?\n", fname, (long long)pos);
            break;
        }
    }

    struct timeval stop;
    gettimeofday(&stop, NULL);

    double res = (stop.tv_sec + stop.tv_usec/1000000.) - (start.tv_sec + start.tv_usec/1000000.);

    if (sequential) {
        size_t const sz = nb_blocks * block_size;
        if (verbose) {
            printf("%zu bytes read in %f seconds: %g bytes/seconds\n", sz, res, sz/res);
        } else {
            printf("%g\n", sz/res);
        }
    } else {
        if (verbose) {
            printf("%u seeks in %f seconds: %g seconds/seeks\n", b, res, res/b);
        } else {
            printf("%g\n", res/b);
        }
    }
quit:
    (void)close(fd);
}

static void usage(void)
{
    printf(
        "diskperf [options...] dev...\n"
        "  Display seek average latency of the given file(s) or block device(s)\n"
        "\n"
        "  options:\n"
        "   -h, --help:         display this help\n"
        "   -v, --verbose:      more verbose output\n"
        "   -n, --nb-blocks N:  reads N blocks (default: 1000)\n"
        "   -s, --block-size S: read blocks of S bytes (default: 4096)\n"
        "   -d, --drop-cache:   drop caches before performing the reads (but not during the reads)\n"
        "   -l, --sequential:   reads sequentialy instead of at random\n"
        "   -a, --noatime:      opens the file with NOATIME option\n"
    );
}

int main(int nb_args, char **args)
{
    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            { "help",       no_argument,       NULL, 'h' },
            { "drop-cache", no_argument,       NULL, 'd' },
            { "verbose",    no_argument,       NULL, 'v' },
            { "noatime",    no_argument,       NULL, 'a' },
            { "sequential", no_argument,       NULL, 'l' },
            { "nb-blocks",  required_argument, NULL, 'n' },
            { "block-size", required_argument, NULL, 's' },
            { NULL,         0,                 NULL, 0 }
        };

        int c = getopt_long(nb_args, args, "hdvaln:s:", long_options, &option_index);
        if (c == -1) break; // done

        switch (c) {
            case 'h':
                usage();
                return EXIT_SUCCESS;
            case 'd':
                drop_caches = true;
                break;
            case 'v':
                verbose = true;
                break;
            case 'a':
                noatime = true;
                break;
            case 'l':
                sequential = true;
                break;
            case 'n':
                nb_blocks = strtoull(optarg, NULL, 0);
                break;
            case 's':
                block_size = strtoull(optarg, NULL, 0);
                break;
            case '?':
                usage();
                return EXIT_FAILURE;
            default:
                printf("?? getopt returned character code 0%o ??\n", c);
        }
    }

    if (optind >= nb_args) {
        printf("nothing to do, done.\n");
        return EXIT_FAILURE;
    }

    srand(time(NULL));

    for (; optind < nb_args; optind ++) {
        do_time_dev(args[optind]);
    }


    return EXIT_SUCCESS;
}
