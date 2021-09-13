#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

#include <TopoDS_Builder.hxx>
#include <TopoDS_CompSolid.hxx>

#include "BOPAlgo_PaveFiller.hxx"
#include "BRepAlgoAPI_Section.hxx"
#include "BRepTools.hxx"
#include "TopoDS_Compound.hxx"

#include "document.hpp"
#include "utils.hpp"

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
	doc.load_brep_file("input.brep");

	TopoDS_Builder builder;
	TopoDS_Compound merged;
	builder.MakeCompound(merged);

    std::string row;
    while (!std::cin.eof()) {
		std::getline(std::cin, row);
		// make sure we didn't get a blank line
		if (std::cin.fail()) {
			break;
		}
		if (std::cin.bad()) {
			spdlog::critical("failed to read line");
			return 1;
		}

		auto fields = parse_csv_row(row);

		if (fields.size() < 2) {
			spdlog::critical("CSV input does not contain two fields");
			return 1;
		}

		int hi, lo;
		if (!int_of_string(fields[0].c_str(), hi) ||
			hi < 0 || (size_t)hi >= doc.solid_shapes.size())
		{
			spdlog::critical("first value is not a valid shape index");
			return 1;
		}

		if (!int_of_string(fields[1].c_str(), lo) ||
			lo < 0 || (size_t)lo >= doc.solid_shapes.size())
		{
			spdlog::critical("second value is not a valid shape index");
			return 1;
		}

		spdlog::info("{:5}-{:<5} processing", hi, lo);

		const auto
			shape = doc.solid_shapes[hi],
			tool = doc.solid_shapes[lo];

		BOPAlgo_PaveFiller filler;

		{
			TopTools_ListOfShape args;
			args.Append(shape);
			args.Append(tool);
			filler.SetArguments(args);
		}

		filler.SetRunParallel(false);
		filler.SetFuzzyValue(0);
		filler.SetNonDestructive(true);

		// this can be a very expensive call, e.g. 10+ seconds
		filler.Perform();

		if(filler.HasErrors()) {
			spdlog::critical("unable to pave shapes");
			return 1;
		}

		BRepAlgoAPI_Section op{shape, tool, filler, false};
		op.SetOperation(BOPAlgo_COMMON);
		op.SetFuzzyValue(filler.FuzzyValue());
		op.SetNonDestructive(true);
		op.SetRunParallel(false);

		op.Build();
		if(!op.IsDone()) {
			spdlog::critical("unable determine solid common to shapes");
			return 1;
		}

		builder.Add(merged, op.Shape());
	}

	if (!BRepTools::Write(merged, path_out.c_str())) {
		spdlog::critical("failed to write brep file");
		return 1;
	}

	return 0;
}
