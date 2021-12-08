#include <istream>
#include <string>
#include <vector>

#include <cxx_argp_parser.h>


class tool_argp_parser : public cxx_argp::parser {
public:
	tool_argp_parser(size_t expected_args = 0);
};


void configure_aixlog();

std::string indexpair_to_string(size_t left, size_t right);

bool int_of_string (const char *s, int &i, int base=0);
bool size_t_of_string (const char *s, size_t &i, int base=0);

bool are_vals_close(double a, double b, double drel=1e-10, double dabs=1e-13);

enum class input_status {
	error,

	end_of_file,

	success,
};

std::vector<std::string> parse_csv_row(const std::string &row);

input_status parse_next_row(std::istream &is, std::vector<std::string> &row);
