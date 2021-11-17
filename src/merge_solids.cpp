#include <spdlog/spdlog.h>

#include <BRepTools.hxx>
#include <TopoDS_Builder.hxx>
#include <TopoDS_Compound.hxx>

#include "salome/geom_gluer.hxx"

#include "document.hpp"


int
main()
{
	document doc;
	doc.load_brep_file("test.brep");

	TopoDS_Compound merged;
	TopoDS_Builder builder;
	builder.MakeCompound(merged);
	for (const auto &shape : doc.solid_shapes) {
		builder.Add(merged, shape);
	}

	const auto resultt = salome_glue_shape(merged, 0.001);

	const char *path = "merged.brep";
	spdlog::info("writing brep file {}", path);
	if (!BRepTools::Write(merged, path)) {
		spdlog::critical("failed to write brep file");
		std::exit(1);
	}

	return 0;
}
