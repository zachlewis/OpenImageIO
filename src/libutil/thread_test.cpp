// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO


#include <algorithm>
#include <functional>
#include <iostream>

#include <OpenImageIO/argparse.h>
#include <OpenImageIO/benchmark.h>
#include <OpenImageIO/sysutil.h>
#include <OpenImageIO/thread.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/unittest.h>
#include <OpenImageIO/ustring.h>

using namespace OIIO;

static int iterations     = 100000;
static int numthreads     = 16;
static int ntrials        = 1;
static bool verbose       = false;
static bool wedge         = false;
static int threadcounts[] = { 1,  2,  4,  8,  12,  16,   20,
                              24, 28, 32, 64, 128, 1024, 1 << 30 };



static void
getargs(int argc, char* argv[])
{
    // clang-format off
    ArgParse ap;
    ap.intro("thread_test\n" OIIO_INTRO_STRING)
      .usage("thread_test [options]");

    ap.arg("-v", &verbose)
      .help("Verbose mode");
    ap.arg("--threads %d", &numthreads)
      .help(Strutil::fmt::format("Number of threads (default: {})", numthreads));
    ap.arg("--iters %d", &iterations)
      .help(Strutil::fmt::format("Number of iterations (default: {})", iterations));
    ap.arg("--trials %d", &ntrials)
      .help("Number of trials");
    ap.arg("--wedge", &wedge)
      .help("Do a wedge test");
    // clang-format on

    ap.parse(argc, (const char**)argv);
}



void
do_nothing(int /*thread_id*/)
{
}



// Exercise the spin locks under real contention.
//
// This serves two purposes. It checks that the locks actually serialize --
// the counter has to land exactly on its expected value, and it does not if
// the lock is removed. And it gives ThreadSanitizer something to analyze:
// the spin locks are built out of atomics rather than a mutex tsan
// recognizes, so without the __tsan_mutex_* annotations in thread.h, tsan
// reports the lock's own internal state as a data race. A tsan build of this
// test is what catches it if those annotations ever regress.
//
// The DoNotOptimize() calls matter: at -O3 the compiler otherwise hoists the
// whole loop into a single `counter += per_thread`, and then even an
// unlocked version passes, because one add per thread rarely collides.
static void
test_spin_lock_contention()
{
    const int nt         = std::min(numthreads, 8);
    const int per_thread = 100000;

    {
        // spin_mutex: every increment is exclusive, so no count can be lost.
        spin_mutex mutex;
        long long counter = 0;
        thread_group g;
        for (int i = 0; i < nt; ++i)
            g.create_thread([&]() {
                for (int j = 0; j < per_thread; ++j) {
                    spin_lock lock(mutex);
                    ++counter;
                    DoNotOptimize(counter);
                }
            });
        g.join_all();
        OIIO_CHECK_EQUAL(counter, (long long)nt * per_thread);
    }

    {
        // spin_rw_mutex: writers exclusive, readers shared and concurrent
        // with each other. Readers observe the counter under a read lock,
        // which is only race-free if the lock really does exclude writers.
        spin_rw_mutex mutex;
        long long counter = 0;
        int nwriters      = std::max(1, nt / 2);
        int nreaders      = std::max(1, nt - nwriters);
        thread_group g;
        for (int i = 0; i < nwriters; ++i)
            g.create_thread([&]() {
                for (int j = 0; j < per_thread; ++j) {
                    spin_rw_write_lock lock(mutex);
                    ++counter;
                    DoNotOptimize(counter);
                }
            });
        for (int i = 0; i < nreaders; ++i)
            g.create_thread([&]() {
                for (int j = 0; j < per_thread; ++j) {
                    spin_rw_read_lock lock(mutex);
                    // Read under the shared lock. Deliberately kept
                    // thread-local: writing a shared result here would be an
                    // unsynchronized write, i.e. a real race in the test
                    // itself rather than in what it is testing.
                    DoNotOptimize(counter);
                }
            });
        g.join_all();
        OIIO_CHECK_EQUAL(counter, (long long)nwriters * per_thread);
    }
}



void
time_thread_group()
{
    std::cout << "\nTiming how long it takes to start/end thread_group:\n";
    std::cout << "threads\ttime (best of " << ntrials << ")\n";
    std::cout << "-------\t----------\n";
    for (int i = 0; threadcounts[i] <= numthreads; ++i) {
        int nt  = wedge ? threadcounts[i] : numthreads;
        int its = iterations / nt;

        // make a lambda function that spawns a bunch of threads, calls a
        // trivial function, then waits for them to finish and tears down
        // the group.
        auto func = [=]() {
            thread_group g;
            for (int i = 0; i < nt; ++i)
                g.create_thread(do_nothing, i);
            g.join_all();
        };

        double range;
        double t = time_trial(func, ntrials, its, &range);

        Strutil::printf("%2d\t%5.1f   launch %8.1f threads/sec\n", nt, t,
                        (nt * its) / t);
        if (!wedge)
            break;  // don't loop if we're not wedging
    }
}



void
time_thread_pool()
{
    print("\nTiming how long it takes to launch from thread_pool:\n");
    print("threads\ttime (best of {})\n", ntrials);
    print("-------\t----------\n");
    thread_pool* pool(default_thread_pool());
    for (int i = 0; threadcounts[i] <= numthreads; ++i) {
        int nt = wedge ? threadcounts[i] : numthreads;
        pool->resize(nt);
        int its = iterations / nt;

        // make a lambda function that spawns a bunch of threads, calls a
        // trivial function, then waits for them to finish and tears down
        // the group.
        auto func = [=]() {
            task_set taskset(pool);
            for (int i = 0; i < nt; ++i) {
                taskset.push(pool->push(do_nothing));
            }
            taskset.wait();
        };

        double range;
        double t = time_trial(func, ntrials, its, &range);

        print("{:2}\t{:5.1f}   launch {:8.1f} threads/sec\n", nt, t,
              (nt * its) / t);
        if (!wedge)
            break;  // don't loop if we're not wedging
    }

    Benchmarker bench;
    bench("std::this_thread::get_id()",
          [=]() { DoNotOptimize(std::this_thread::get_id()); });
    std::thread::id threadid = std::this_thread::get_id();
    bench("register/deregister pool worker", [=]() {
        pool->register_worker(threadid);
        pool->deregister_worker(threadid);
    });
}



int
main(int argc, char** argv)
{
#if !defined(NDEBUG) || defined(OIIO_CI) || defined(OIIO_CODE_COVERAGE)
    // For the sake of test time, reduce the default iterations for DEBUG,
    // CI, and code coverage builds. Explicit use of --iters or --trials
    // will override this, since it comes before the getargs() call.
    iterations /= 10;
    ntrials = 1;
#endif

    getargs(argc, argv);

    std::cout << "hw threads = " << Sysutil::hardware_concurrency() << "\n";

    test_spin_lock_contention();

    time_thread_group();
    time_thread_pool();

    return unit_test_failures;
}
