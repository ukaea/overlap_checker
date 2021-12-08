#include <cassert>

#include <aixlog.hpp>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

#include <TopExp_Explorer.hxx>

#include "geometry.hpp"
#include "utils.hpp"


int
main(int argc, char **argv)
{
	configure_aixlog();

	std::string path_in, path_out;

	{
		const char *doc = "Flatten contents of BREP file, producing a file usable by other tools.";
		const char *usage = "input.step output.brep";

		tool_argp_parser argp(2);

		if (!argp.parse(argc, argv, usage, doc)) {
			return 1;
		}

		const auto &args = argp.arguments();
		assert(args.size() == 2);
		path_in = args[0];
		path_out = args[1];
	}

	TopoDS_Shape shape;
	if (!BRepTools::Read(shape, path_in.c_str(), BRep_Builder{})) {
		LOG(FATAL) << "failed to load brep file\n";
		std::exit(1);
	}

	LOG(DEBUG) << "read brep file " << path_in << '\n';

	document doc;
	for (TopExp_Explorer ex{shape, TopAbs_SOLID}; ex.More(); ex.Next()) {
		doc.solid_shapes.emplace_back(ex.Current());
	}

	LOG(INFO) << "found " << doc.solid_shapes.size() << " solids\n";

	doc.write_brep_file(path_out.c_str());

	return 0;
}
