#include <cerrno>
#include <climits>
#include <cassert>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#ifdef INCLUDE_TESTS
#include <catch2/catch_test_macros.hpp>
#endif

#include <aixlog.hpp>
#include <cxx_argp_parser.h>

#include "utils.hpp"

static auto aixlog_severity = AixLog::Severity::info;
static std::shared_ptr<AixLog::Sink> aixlog_sink;

#define OPT_USAGE -3

tool_argp_parser::tool_argp_parser(size_t expected_args) : cxx_argp::parser(expected_args)
{
	add_flags(ARGP_NO_HELP);
	help_via_argp_flags = false;

	add_option(
		{"verbose", 'v', nullptr, 0, "Increase verbosity of output, can be repeated", -2},
		[](int, const char *, struct argp_state*) {
			// go through severity levels:  Info => Debug => Trace
			if (aixlog_severity > AixLog::Severity::debug){
				if (aixlog_severity > AixLog::Severity::info) {
					aixlog_severity = AixLog::Severity::info;
				} else {
					aixlog_severity = AixLog::Severity::debug;
				}
			} else if (aixlog_severity > AixLog::Severity::trace) {
				aixlog_severity = AixLog::Severity::trace;
			}
			aixlog_sink->filter.add_filter(aixlog_severity);
			return 0;
		});

	add_option(
		{"quiet", 'q', nullptr, 0, "Decrease verbosity of output", -2},
		[](int, const char *, struct argp_state*) {
			aixlog_sink->filter.add_filter(
				aixlog_severity = AixLog::Severity::warning);
			return 0;
		});

	add_option(
		{"help", 'h', nullptr, 0, "Give this help list", -1},
		[](int, const char *, struct argp_state* state) {
			argp_state_help(state, stderr, ARGP_HELP_STD_HELP);
			std::exit(0);
			return 0;
		});

	add_option(
		{"usage", OPT_USAGE, nullptr, 0, "Give a short usage message", -1},
		[](int, const char *, struct argp_state* state) {
			argp_state_help(state, stderr, ARGP_HELP_STD_USAGE);
			std::exit(0);
			return 0;
		});
}


void configure_aixlog()
{
	using timepoint = AixLog::Timestamp::time_point_sys_clock;

	timepoint reference = timepoint::clock::now();

	auto callback = [reference](const AixLog::Metadata& metadata, const std::string& message) {
		if (metadata.timestamp) {
			auto elapsed = std::chrono::duration<double>(metadata.timestamp.time_point - reference);
			std::cerr << std::fixed << std::setprecision(3) << elapsed.count();
		}
		std::cerr << " [" << AixLog::to_string(metadata.severity) << "] ";
		if (metadata.tag) {
			std::cerr << '(' << metadata.tag.text << ") ";
		}
		std::cerr << message << '\n';
	};

	aixlog_sink = AixLog::Log::init<AixLog::SinkCallback>(aixlog_severity, callback);
}

std::string indexpair_to_string(size_t left, size_t right)
{
	std::stringstream ss;
	ss.width(5);
	ss << left << '-' << std::left << right;
	return ss.str();
}


/** are floats close, i.e. approximately equal? due to floating point
 * representation we have to care about a couple of types of error, relative
 * and absolute.
 */
bool
are_vals_close(const double a, const double b, const double drel, const double dabs)
{
	assert(drel >= 0);
	assert(dabs >= 0);
	assert(drel > 0 || dabs > 0);

	const auto mag = std::max(std::abs(a), std::abs(b));

	return std::abs(b - a) < (drel * mag + dabs);
}

#ifdef INCLUDE_TESTS
TEST_CASE("are_vals_close") {
	SECTION("identical values") {
		CHECK(are_vals_close(0, 0));
		CHECK(are_vals_close(1, 1));
	}
	SECTION("close values") {
		CHECK(are_vals_close(0, 1e-15));
		CHECK(are_vals_close(1, 1+1e-15));
	}
	SECTION("far values") {
		CHECK_FALSE(are_vals_close(0, 1));
		CHECK_FALSE(are_vals_close(1, 0));
		CHECK_FALSE(are_vals_close(0, 1e-10));
	}
}
#endif

bool int_of_string(const char *s, int &i, int base)
{
	char *end;
	long  l;
	errno = 0;
	l = strtol(s, &end, base);
	if ((errno == ERANGE && l == LONG_MAX) || l > std::numeric_limits<int>::max()) {
		return false;
	}
	if ((errno == ERANGE && l == LONG_MIN) || l < std::numeric_limits<int>::min()) {
		return false;
	}
	if (*s == '\0' || *end != '\0') {
		return false;
	}
	i = (int)l;
	return true;
}

bool size_t_of_string(const char *s, size_t &i, int base)
{
	char *end;
	long  l;
	errno = 0;
	l = strtol(s, &end, base);
	if (errno == ERANGE && l == LONG_MAX) {
		return false;
	}
	if ((errno == ERANGE && l == LONG_MIN) || l < 0) {
		return false;
	}
	if (*s == '\0' || *end != '\0') {
		return false;
	}
	i = (size_t)l;
	return true;
}

#ifdef INCLUDE_TESTS
TEST_CASE("int_of_string") {
	SECTION("success") {
		int val = -1;
		CHECK(int_of_string("0", val));
		CHECK(val == 0);
		CHECK(int_of_string("1", val));
		CHECK(val == 1);
		CHECK(int_of_string("-1", val));
		CHECK(val == -1);
		CHECK(int_of_string("0x10", val));
		CHECK(val == 16);
		CHECK(int_of_string("ff", val, 16));
		CHECK(val == 255);
	}
	SECTION("failure") {
		int val = -1;
		CHECK_FALSE(int_of_string("", val));
		CHECK(val == -1);
		CHECK_FALSE(int_of_string("zzz", val));
		CHECK(val == -1);
	}
}
TEST_CASE("size_t_of_string") {
	SECTION("success") {
		size_t val = 1;
		CHECK(size_t_of_string("0", val));
		CHECK(val == 0);
		CHECK(size_t_of_string("1", val));
		CHECK(val == 1);
		CHECK(size_t_of_string("0x10", val));
		CHECK(val == 16);
		CHECK(size_t_of_string("ff", val, 16));
		CHECK(val == 255);
	}
	SECTION("failure") {
		size_t val = 7;
		CHECK_FALSE(size_t_of_string("", val));
		CHECK(val == 7);
		CHECK_FALSE(size_t_of_string("-1", val));
		CHECK(val == 7);
		CHECK_FALSE(size_t_of_string("18446744073709551616", val));
		CHECK(val == 7);
		CHECK_FALSE(size_t_of_string("zzz", val));
		CHECK(val == 7);
	}
}
#endif

enum class CSVState {
	UnquotedField,
	QuotedField,
	QuotedQuote
};

// copied from https://stackoverflow.com/a/30338543/1358308
std::vector<std::string>
parse_csv_row(const std::string &row)
{
	CSVState state = CSVState::UnquotedField;
	std::vector<std::string> fields {""};
	size_t i = 0; // index of the current field
	for (char c : row) {
		switch (state) {
		case CSVState::UnquotedField:
			switch (c) {
			case ',': // end of field
				fields.push_back(""); i++;
				break;
			case '"':
				state = CSVState::QuotedField;
				break;
			default:  fields[i].push_back(c);
				break; }
			break;
		case CSVState::QuotedField:
			switch (c) {
			case '"':
				state = CSVState::QuotedQuote;
				break;
			default:  fields[i].push_back(c);
				break; }
			break;
		case CSVState::QuotedQuote:
			switch (c) {
			case ',': // , after closing quote
				fields.push_back(""); i++;
				state = CSVState::UnquotedField;
				break;
			case '"': // "" -> "
				fields[i].push_back('"');
				state = CSVState::QuotedField;
				break;
			default:  // end of quote
				state = CSVState::UnquotedField;
				break; }
			break;
		}
	}
	return fields;
}

#ifdef INCLUDE_TESTS
template<typename T> bool
vectors_eq(const std::vector<T> &a, const std::vector<T> &b)
{
	return a.size() == b.size() &&
		std::equal(a.begin(), a.end(), b.begin());
}

TEST_CASE("parse_csv_row") {
	SECTION("validate vectors_eq") {
		CHECK(vectors_eq<int>({}, {}));
		CHECK(vectors_eq<int>({0}, {0}));
		CHECK_FALSE(vectors_eq<int>({0}, {}));
		CHECK_FALSE(vectors_eq<int>({}, {1}));
		CHECK_FALSE(vectors_eq<int>({0}, {1}));
	}
	SECTION("simple cases") {
		CHECK(vectors_eq(parse_csv_row(""), {""}));
		CHECK(vectors_eq(parse_csv_row("a"), {"a"}));
		CHECK(vectors_eq(parse_csv_row(","), {"",""}));
		CHECK(vectors_eq(parse_csv_row(",a"), {"","a"}));
		CHECK(vectors_eq(parse_csv_row("a, b"), {"a"," b"}));
		CHECK(vectors_eq(parse_csv_row("a ,b"), {"a ","b"}));
	}
	SECTION("double-quote escapes") {
		CHECK(vectors_eq(parse_csv_row("\"\""), {""}));
		CHECK(vectors_eq(parse_csv_row("\",\""), {","}));
		CHECK(vectors_eq(parse_csv_row("\"\"\"\""), {"\""}));
	}
}
#endif


input_status
parse_next_row(std::istream &is, std::vector<std::string> &row)
{
	if (is.eof()) {
		return input_status::end_of_file;
	}
	std::string line;
	if (std::getline(is, line).fail()) {
		return input_status::end_of_file;
	}
	if (is.bad()) {
		return input_status::error;
	}

	row = parse_csv_row(line);

	return input_status::success;
}
