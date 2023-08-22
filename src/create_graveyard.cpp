#include <cassert>

#include <aixlog.hpp>

#include <TopoDS_Shape.hxx>

#include "geometry.hpp"
#include "utils.hpp"


int
main(int argc, char **argv)
{
	configure_aixlog();

	std::string path_in, path_out;

	{
		const char * doc = (
			"Add graveyard volume surrounding all other volumes.");
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

	document doc;
	doc.load_brep_file(path_in.c_str());

	{
		const TopoDS_Shape & graveyard = doc.create_graveyard();
		doc.solid_shapes.push_back(graveyard);		
	}

	doc.write_brep_file(path_out.c_str());

	return 0;
}
