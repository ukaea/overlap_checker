#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

#include <TopoDS_Builder.hxx>
#include <TopoDS_Compound.hxx>

#include <BRepAlgoAPI_Section.hxx>
#include <BRepTools.hxx>

#include "document.hpp"
#include "utils.hpp"

static int
merge_into(const document &doc, TopoDS_Compound &merged)
{
	TopoDS_Builder builder;
	builder.MakeCompound(merged);

	input_status status;
	std::vector<std::string> fields;
	while ((status = parse_next_row(std::cin, fields)) == input_status::success) {
		if (fields.size() < 2) {
			spdlog::critical("CSV input does not contain two fields");
			return 1;
		}

		int first, second;

		if ((first = doc.lookup_solid(fields[0])) < 0) {
			spdlog::critical("first value ({}) is not a valid shape index", fields[0]);
			return 1;
		}

		if ((second = doc.lookup_solid(fields[1])) < 0) {
			spdlog::critical("second value ({}) is not a valid shape index", fields[1]);
			return 1;
		}

		spdlog::info("{:5}-{:<5} processing", first, second);

		// using Section just because it exposes PerformNow
		BRepAlgoAPI_Section op{
			doc.solid_shapes[first], doc.solid_shapes[second], false};
		op.SetOperation(BOPAlgo_COMMON);
		op.SetFuzzyValue(0);
		op.SetNonDestructive(true);
		op.SetRunParallel(false);

		op.Build();
		if(!op.IsDone()) {
			spdlog::critical("unable determine solid common to shapes");
			return 1;
		}

		builder.Add(merged, op.Shape());
	}

	if (status == input_status::end_of_file) {
		return 0;
	} else {
		spdlog::critical("failed to read line");
		return 1;
	}
}

int
main(int argc, char **argv)
{
	configure_spdlog();

	std::string path_in, path_out;

	{
		CLI::App app{"Collect overlapping areas of solids and write to BREP file."};
		app.add_option("input", path_in, "Path of the input file")
			->required()
			->option_text("input.brep");
		app.add_option("output", path_out, "Path of the output file")
			->required()
			->option_text("output.brep");
		CLI11_PARSE(app, argc, argv);
	}

	document doc;
	doc.load_brep_file(path_in.c_str());

	TopoDS_Compound merged;
	const int status = merge_into(doc, merged);
	if (status != 0) {
		return status;
	}

	spdlog::info("writing brep file {}", path_out);
	if (!BRepTools::Write(merged, path_out.c_str())) {
		spdlog::critical("failed to write brep file");
		return 1;
	}

	return 0;
}
