#include <error.h>
#include <getopt.h>
#include <errno.h>
#include <sys/resource.h>

#include <cstdlib>
#include <cassert>

#include "config.h"
#include "debug.hpp"
#include "runtime.hpp"
#include "arch.hpp"
#include "instance_scheduler.hpp"
#include "partitioned_scheduler.hpp"
#include "generate_code.hpp"
#include "allocate.hpp"
#include "scheduler.hpp"
#include "scope.hpp"
#include "error_reporter.hpp"
#include "symbol_table.hpp"
#include "package_set.hpp"
#include "topological_sort.hpp"
#include "package.hpp"
#include "source_file.hpp"

static void
print_version (void)
{
  std::cout <<
            PACKAGE_STRING "\n"
            "Copyright (C) 2014 Justin R. Wilson\n"
            "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
            "This is free software: you are free to change and redistribute it.\n"
            "There is NO WARRANTY, to the extent permitted by law.\n";
}

static void
try_help (void)
{
  std::cerr << "Try `" << program_invocation_short_name << " --help' for more information.\n";
  exit (EXIT_FAILURE);
}

#define SCHEDULER_OPTION 256
#define THREADS_OPTION 257
#define SRAND_OPTION 258
#define PROFILE_OPTION 259
#define PROFILE_OUT_OPTION 260

int
main (int argc, char **argv)
{
  int show_composition = 0;
  // TODO:  Get the number of cores from the system.
  int thread_count = 2;
  std::string scheduler_type = "partitioned";
  // Profile stores the number of points to record per thread.
  // It must be a power of two.  This makes the ring buffer index calculation easier because the modulus can be replaced by bit-and.
  size_t profile = 0;
  FILE* profile_out = stderr;
  std::string rcgo_root;
  const char* s;

  s = getenv ("RCGO_ROOT");
  if (s == NULL)
    {
      error (EXIT_FAILURE, 0, "RCGO_ROOT is not set");
    }
  rcgo_root = s;

  while (rcgo_root[rcgo_root.size () - 1] == '/')
    {
      rcgo_root = rcgo_root.substr (0, rcgo_root.size () - 1);
    }

  s = getenv ("RCGO_SCHEDULER");
  if (s != NULL)
    {
      scheduler_type = s;
    }

  while (true)
    {
      static struct option long_options[] =
      {
        {"help",        no_argument, NULL, 'h'},
        {"version",     no_argument, NULL, 'v'},

        {"composition", no_argument, &show_composition, 1},

        {"scheduler",   required_argument, NULL, SCHEDULER_OPTION},
        {"threads",     required_argument, NULL, THREADS_OPTION},
        {"srand",       required_argument, NULL, SRAND_OPTION},
        {"profile",     optional_argument, NULL, PROFILE_OPTION},
        {"profile-out", required_argument, NULL, PROFILE_OUT_OPTION},

        {0, 0, 0, 0}
      };

      int c = getopt_long (argc, argv, "hv", long_options, NULL);

      if (c == -1)
        break;

      switch (c)
        {
        case 0:
          break;
        case 'h':
          std::cout << "Usage: " << program_invocation_short_name << " OPTION... FILE\n"
                    <<
                    PACKAGE_NAME "\n"
                    "\n"
                    "  --composition       print composition analysis and exit\n"
                    "  --scheduler=SCHED   select a scheduler (instance, partitioned)\n"
                    "  --threads=NUM       use NUM threads\n"
                    "  --srand=NUM         initialize the random number generator with NUM\n"
                    "  --profile[=SIZE]    enable profiling and store at least SIZE points per thread\n"
                    "                      when profiling (4096)\n"
                    "  --profile-out=FILE  write profiling data to FILE (stderr)\n"
                    "  -h, --help          display this help and exit\n"
                    "  -v, --version       display version information and exit\n"
                    "\n"
                    "Report bugs to: " PACKAGE_BUGREPORT "\n";
          exit (EXIT_SUCCESS);
          break;
        case 'v':
          print_version ();
          exit (EXIT_SUCCESS);
          break;

        case SCHEDULER_OPTION:
          scheduler_type = optarg;
          break;
        case THREADS_OPTION:
          thread_count = atoi (optarg);
          break;
        case SRAND_OPTION:
          srand (atoi (optarg));
          break;
        case PROFILE_OPTION:
          if (optarg)
            {
              profile = atoi (optarg);
            }
          else
            {
              profile = 4096;
            }
          break;
        case PROFILE_OUT_OPTION:
          profile_out = fopen (optarg, "w");
          if (profile_out == NULL)
            {
              error (EXIT_FAILURE, errno, "Could not open %s for writing", optarg);
            }
          break;

        default:
          try_help ();
          break;
        }
    }

  // TODO:  Check if thread count exceeds cores.
  if (thread_count < 0 || thread_count > 64)
    {
      error (EXIT_FAILURE, 0, "Illegal thread count: %d", thread_count);
    }

  if (profile)
    {
      fprintf (profile_out, "BEGIN profile\n");
      fprintf (profile_out, "scheduler %s\n", scheduler_type.c_str ());

      unsigned int e = 1;
      while ((1U << e) < profile && e < 31)
        {
          ++e;
        }
      profile = 1 << e;
      fprintf (profile_out, "points_per_thread %zd\n", profile);

      struct timespec res;
      clock_getres (CLOCK_MONOTONIC, &res);
      fprintf (profile_out, "resolution %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
    }

  if (profile)
    {
      struct timespec res;
      clock_gettime (CLOCK_MONOTONIC, &res);
      fprintf (profile_out, "BEGIN parse %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
    }

  source::PackageSet package_set;
  for (; optind != argc; ++optind)
    {
      package_set.load (rcgo_root, argv[optind]);
    }

  if (profile)
    {
      struct timespec res;
      clock_gettime (CLOCK_MONOTONIC, &res);
      fprintf (profile_out, "END parse %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
    }

  if (package_set.empty ())
    {
      std::cerr << "No packages\n";
      try_help ();
    }

  if (profile)
    {
      struct timespec res;
      clock_gettime (CLOCK_MONOTONIC, &res);
      fprintf (profile_out, "BEGIN semantic_analysis %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
    }

  // Sort the package topologically.
  // TODO:  Check for error.
  util::topological_sort (package_set.packages_begin (), package_set.packages_end ());

  // TODO:  When do we need to set this?
  arch::set_stack_alignment (sizeof (void*));

  util::ErrorReporter er (3);

  package_set.determine_package_names (er);
  package_set.process_symbols (er);

  // // A cycle means there is a recursive definition.

  // // Process the definitions in topological order.

  // // Process all top-level declarations.
  // // This includes constants, types, functions, methods, initializers, getters,
  // // actions, reactions, binders, and instances.
  // semantic::process_top_level_declarations (root, er, symbol_table);

  // // Establish a type for every expression and values for constant expressions.
  // // Check for rvalues and lvalues and determine where a dereference is needed.
  // // Characterize the mutability of each expression and check for const-correctness.
  // // Characterize how activations use the state of the receiver.

  // semantic::check_all (root, er, symbol_table);
  // semantic::compute_receiver_access (root);

  // if (profile)
  //   {
  //     struct timespec res;
  //     clock_gettime (CLOCK_MONOTONIC, &res);
  //     fprintf (profile_out, "END semantic_analysis %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
  //   }

  // if (profile)
  //   {
  //     struct timespec res;
  //     clock_gettime (CLOCK_MONOTONIC, &res);
  //     fprintf (profile_out, "BEGIN code_generation %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
  //   }

  // // Calculate the offsets of all stack variables.
  // // TODO:  Allocate and generate code after composition check.
  // // Do this so we can execute some code statically when checking composition.
  // code::allocate_stack_variables (root);

  // // Generate code.
  // code::generate_code (root);

  // if (profile)
  //   {
  //     struct timespec res;
  //     clock_gettime (CLOCK_MONOTONIC, &res);
  //     fprintf (profile_out, "END code_generation %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
  //   }

  // if (profile)
  //   {
  //     struct timespec res;
  //     clock_gettime (CLOCK_MONOTONIC, &res);
  //     fprintf (profile_out, "BEGIN composition_check %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
  //   }

  // // Check composition.
  // composition::Composer instance_table;
  // instance_table.enumerate_instances (root);
  // instance_table.elaborate ();
  // if (show_composition)
  //   {
  //     instance_table.dump_graphviz ();
  //     return 0;
  //   }
  // instance_table.analyze ();

  // if (profile)
  //   {
  //     struct timespec res;
  //     clock_gettime (CLOCK_MONOTONIC, &res);
  //     fprintf (profile_out, "END composition_check %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
  //   }

  // if (profile)
  //   {
  //     struct timespec res;
  //     clock_gettime (CLOCK_MONOTONIC, &res);
  //     fprintf (profile_out, "BEGIN scheduler_init %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
  //   }

  // runtime::allocate_instances (instance_table);
  // runtime::create_bindings (instance_table);

  // runtime::Scheduler* scheduler;
  // if (scheduler_type == "partitioned")
  //   {
  //     scheduler = new runtime::partitioned_scheduler_t ();
  //   }
  // else if (scheduler_type == "instance")
  //   {
  //     scheduler = new runtime::instance_scheduler_t ();
  //   }
  // else
  //   {
  //     error (EXIT_FAILURE, 0, "unknown scheduler type '%s'", scheduler_type.c_str ());
  //   }

  // scheduler->init (instance_table, 8 * 1024, thread_count, profile);

  // if (profile)
  //   {
  //     struct timespec res;
  //     clock_gettime (CLOCK_MONOTONIC, &res);
  //     fprintf (profile_out, "END scheduler_init %ld.%.09ld\n", res.tv_sec, res.tv_nsec);
  //   }

  // if (profile)
  //   {
  //     struct rusage usage;
  //     getrusage (RUSAGE_SELF, &usage);
  //     struct timespec res;
  //     clock_gettime (CLOCK_MONOTONIC, &res);
  //     fprintf (profile_out, "BEGIN scheduler_run %ld.%.09ld %ld %ld\n", res.tv_sec, res.tv_nsec, usage.ru_nvcsw, usage.ru_nivcsw);
  //   }

  // scheduler->run ();

  // if (profile)
  //   {
  //     struct timespec res;
  //     clock_gettime (CLOCK_MONOTONIC, &res);
  //     struct rusage usage;
  //     getrusage (RUSAGE_SELF, &usage);
  //     fprintf (profile_out, "END scheduler_run %ld.%.09ld %ld %ld\n", res.tv_sec, res.tv_nsec, usage.ru_nvcsw, usage.ru_nivcsw);
  //   }

  // scheduler->fini (profile_out);

  // if (profile)
  //   {
  //     fprintf (profile_out, "END profile\n");
  //   }

  return 0;
}
