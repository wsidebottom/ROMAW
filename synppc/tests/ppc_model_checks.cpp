/*+===================================================================
  File:      ppc_model_checks.cpp

  Summary:   Run and report the generated PowerPC arithmetic model checks in one process.

  Classes:   None.

  Functions: main

  Origin:    Consolidates the existing synppc host regression fixtures.

  Copyright and Legal notices: See synppc/COPYING.
===================================================================+*/

#include "ppc_model_checks.h"
#include <string>

int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <case-directory>\n", argv[0]);
        return 2;
    }

    const struct {
        const char* name;
        int (*run)(FILE*);
    } checks[] = {{"arithmetic", ppc_arithmetic_check},
                  {"rounding-status", ppc_rounding_status_check},
                  {"integer", ppc_integer_check},
                  {"compare", ppc_compare_check},
                  {"fused-status", ppc_fused_status_check}};

    const std::string directory = std::string(argv[1]) + '/';
    for (const auto& check : checks) {
        printf("Running PPC %s model check\n", check.name);
        fflush(stdout);
        auto path = directory + check.name + ".txt";
        FILE* cases = fopen(path.c_str(), "r");
        if (!cases) {
            perror(path.c_str());
            return 1;
        }
        int status = check.run(cases);
        bool read_error = ferror(cases);
        fclose(cases);
        if (status || read_error) {
            fprintf(stderr, "PPC %s model check failed with input %s\n", check.name, path.c_str());
            return status ? status : 1;
        }
    }
    puts("All PPC arithmetic model checks passed");
}
