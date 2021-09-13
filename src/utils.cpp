#ifdef INCLUDE_DOCTESTS
#include <doctest/doctest.h>
#endif

#include <errno.h>
#include <climits>

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <spdlog/stopwatch.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>


#include "utils.hpp"



// code to allow spdlog to print out elapsed time since process started
class time_elapsed_formatter_flag : public spdlog::custom_flag_formatter
{
	using clock = std::chrono::steady_clock;
	using timepoint = std::chrono::time_point<clock>;

	timepoint reference;

public:
	time_elapsed_formatter_flag() : reference{clock::now()} {}
	time_elapsed_formatter_flag(timepoint ref) : reference{ref} {}

	void format(const spdlog::details::log_msg &, const std::tm &, spdlog::memory_buf_t &dest) override {
		auto elapsed = std::chrono::duration<double>(clock::now() - reference);
		auto txt = fmt::format("{:.3f}", elapsed.count());
		dest.append(txt.data(), txt.data() + txt.size());
	}

	std::unique_ptr<custom_flag_formatter> clone() const override {
		return spdlog::details::make_unique<time_elapsed_formatter_flag>(reference);
	}
};

void configure_spdlog()
{
	// pull config from environment variables, e.g. `export SPDLOG_LEVEL=info,mylogger=trace`
	spdlog::cfg::load_env_levels();

	auto formatter = std::make_unique<spdlog::pattern_formatter>();
	formatter->add_flag<time_elapsed_formatter_flag>('*');
	formatter->set_pattern("[%*] [%^%l%$] %v");
	spdlog::set_formatter(std::move(formatter));

	// Replace the default logger with a (color, single-threaded) stderr
	// logger with name "" (but first replace it with an arbitrarily-named
	// logger to prevent a name clash)
	spdlog::set_default_logger(spdlog::stderr_color_mt("some_arbitrary_name"));
	spdlog::set_default_logger(spdlog::stderr_color_mt(""));
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

#ifdef DOCTEST_LIBRARY_INCLUDED
TEST_SUITE("testing are_vals_close") {
	TEST_CASE("identical values") {
		CHECK(are_vals_close(0, 0));
		CHECK(are_vals_close(1, 1));
	}
	TEST_CASE("close values") {
		CHECK(are_vals_close(0, 1e-15));
		CHECK(are_vals_close(1, 1+1e-15));
	}
	TEST_CASE("far values") {
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
	i = l;
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
	i = l;
	return true;
}

#ifdef DOCTEST_LIBRARY_INCLUDED
TEST_SUITE("testing int_of_string") {
	TEST_CASE("success") {
		int val = -1;
		CHECK(int_of_string("0", val));
		CHECK_EQ(val, 0);
		CHECK(int_of_string("1", val));
		CHECK_EQ(val, 1);
		CHECK(int_of_string("-1", val));
		CHECK_EQ(val, -1);
		CHECK(int_of_string("0x10", val));
		CHECK_EQ(val, 16);
		CHECK(int_of_string("ff", val, 16));
		CHECK_EQ(val, 255);
	}
	TEST_CASE("failure") {
		int val = -1;
		CHECK_FALSE(int_of_string("", val));
		CHECK_EQ(val, -1);
		CHECK_FALSE(int_of_string("zzz", val));
		CHECK_EQ(val, -1);
	}
}
TEST_SUITE("testing size_t_of_string") {
	TEST_CASE("success") {
		size_t val = 1;
		CHECK(size_t_of_string("0", val));
		CHECK_EQ(val, 0);
		CHECK(size_t_of_string("1", val));
		CHECK_EQ(val, 1);
		CHECK(size_t_of_string("0x10", val));
		CHECK_EQ(val, 16);
		CHECK(size_t_of_string("ff", val, 16));
		CHECK_EQ(val, 255);
	}
	TEST_CASE("failure") {
		size_t val = 7;
		CHECK_FALSE(size_t_of_string("", val));
		CHECK_EQ(val, 7);
		CHECK_FALSE(size_t_of_string("-1", val));
		CHECK_EQ(val, 7);
		CHECK_FALSE(size_t_of_string("18446744073709551616", val));
		CHECK_EQ(val, 7);
		CHECK_FALSE(size_t_of_string("zzz", val));
		CHECK_EQ(val, 7);
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
