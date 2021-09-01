#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#include "document.hpp"

int
main()
{
	// pull config from environment variables, e.g. `export SPDLOG_LEVEL=info,mylogger=trace`
	spdlog::cfg::load_env_levels();

	const char *inp = "../../data/mastu.stp";

	document doc;
	doc.load_step_file(inp);

	const char *out = "mastu.brep";
	doc.write_brep_file(out);

	spdlog::debug("done");

	return 0;
}
