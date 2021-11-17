#include <spdlog/spdlog.h>

#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

#include <TopExp_Explorer.hxx>

#include "document.hpp"
#include "utils.hpp"


int
main(int argc, char **argv)
{
	configure_spdlog();

	std::string path_in, path_out;

	{
		CLI::App app{"Flatten contents of BREP file, producing a file usable by other tools."};
		app.add_option("input", path_in, "Path of the input file")
			->required()
			->option_text("input.brep");
		app.add_option("output", path_out, "Path of the output file")
			->required()
			->option_text("output.brep");
		CLI11_PARSE(app, argc, argv);
	}

	TopoDS_Shape shape;
	if (!BRepTools::Read(shape, path_in.c_str(), BRep_Builder{})) {
		spdlog::critical("failed to load brep file");
		std::exit(1);
	}

	spdlog::debug("read brep file {}", path_in);

	document doc;
	for (TopExp_Explorer ex{shape, TopAbs_SOLID}; ex.More(); ex.Next()) {
		doc.solid_shapes.emplace_back(ex.Current());
	}

	spdlog::info("found {} solids", doc.solid_shapes.size());

	doc.write_brep_file(path_out.c_str());

	return 0;
}
