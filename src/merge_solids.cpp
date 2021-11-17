#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

#include <BRepTools.hxx>

#include <TopoDS_Iterator.hxx>
#include <TopoDS_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include "TopoDS_Solid.hxx"
#include "TopoDS_Shape.hxx"

#include "salome/geom_gluer.hxx"

#include "document.hpp"
#include "utils.hpp"


int
main(int argc, char **argv)
{
	configure_spdlog();

	std::string path_in, path_out;

	{
		CLI::App app{"Merge surfaces across solids, identical sub-geometry will become shared."};
		app.add_option("input", path_in, "Path of the input file")
			->required()
			->option_text("input.brep");
		app.add_option("output", path_out, "Path of the output file")
			->required()
			->option_text("output.brep");
		CLI11_PARSE(app, argc, argv);
	}

	document inp;
	inp.load_brep_file(path_in.c_str());

	TopoDS_Compound merged;
	TopoDS_Builder builder;
	builder.MakeCompound(merged);
	for (const auto &shape : inp.solid_shapes) {
		builder.Add(merged, shape);
	}

	document out;

	{
		const auto result = salome_glue_shape(merged, 0.001);

		for (TopoDS_Iterator it{result}; it.More(); it.Next()) {
			out.solid_shapes.emplace_back(it.Value());
		}
	}

	spdlog::debug("checking merged shapes are similar to input");

	if (inp.solid_shapes.size() != out.solid_shapes.size()) {
		spdlog::error(
			"number of shapes changed after merge, {} => {}",
			inp.solid_shapes.size(), out.solid_shapes.size());
		std::exit(1);
	}

	size_t num_changed = 0;
	for (size_t i = 0; i < inp.solid_shapes.size(); i++) {
		const double
			v1 = volume_of_shape(inp.solid_shapes[i]),
			v2 = volume_of_shape(out.solid_shapes[i]),
			mn = std::min(v1, v2) * 0.001;

		if (std::fabs(v1 - v2) > mn) {
			spdlog::error(
				"number of shapes changed after merge, {} => {}",
				inp.solid_shapes.size(), out.solid_shapes.size());
			num_changed += 1;
		}
	}

	if (num_changed > 0) {
		std::exit(1);
	}

	out.write_brep_file(path_out.c_str());

	return 0;
}
