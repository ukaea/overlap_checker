#include <string>
#include <vector>

void configure_spdlog();

bool int_of_string (const char *s, int &i, int base=0);
bool size_t_of_string (const char *s, size_t &i, int base=0);

bool are_vals_close(double a, double b, double drel=1e-10, double dabs=1e-13);

std::vector<std::string> parse_csv_row(const std::string &row);
