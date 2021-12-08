#include <algorithm>
#include <cassert>
#include <cmath>

#include <aixlog.hpp>

#include <BRepTools.hxx>

#include <TopoDS_Iterator.hxx>
#include <TopoDS_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include "TopoDS_Solid.hxx"
#include "TopoDS_Shape.hxx"

#include "salome/geom_gluer.hxx"

#include "geometry.hpp"
#include "utils.hpp"


int
main(int argc, char **argv)
{
	configure_aixlog();

	std::string path_in, path_out;

	{
		const char * doc = (
			"Merge surfaces across solids, identical sub-geometry will become shared.");
		const char * usage = "input.brep output.brep";

		tool_argp_parser argp(2);

		if (!argp.parse(argc, argv, usage, doc)) {
			return 1;
		}

		const auto &args = argp.arguments();
		assert(args.size() == 2);
		path_in = args[0];
		path_out = args[1];
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

	LOG(DEBUG) << "checking merged shapes are similar to input\n";

	if (inp.solid_shapes.size() != out.solid_shapes.size()) {
		LOG(ERROR)
			<< "number of shapes changed after merge, "
			<< inp.solid_shapes.size() << " => "
			<< out.solid_shapes.size() << '\n';
		std::exit(1);
	}

	size_t num_changed = 0;
	for (size_t i = 0; i < inp.solid_shapes.size(); i++) {
		const double
			v1 = volume_of_shape(inp.solid_shapes[i]),
			v2 = volume_of_shape(out.solid_shapes[i]),
			mn = std::min(v1, v2) * 0.001;

		if (std::fabs(v1 - v2) > mn) {
			LOG(WARNING)
				<< "non-trivial change in volume during merge, "
				<< v1 << " => " << v2 << '\n';
			num_changed += 1;
		}
	}

	if (num_changed > 0) {
		std::exit(1);
	}

	out.write_brep_file(path_out.c_str());

	return 0;
}
