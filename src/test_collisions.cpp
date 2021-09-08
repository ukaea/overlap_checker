#include <array>
#include <assert.h>
#include <sstream>

#include <spdlog/spdlog.h>

#include <TopoDS_Shape.hxx>

#include <BRepPrimAPI_MakeBox.hxx>

#include "document.hpp"


static void
test_bops()
{
	const TopoDS_Shape
		s1 = BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 10), 10, 10, 10).Shape(),
		s2 = BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 19), 10, 10, 10).Shape();

	spdlog::info(
		"shape vols {:.1f} {:.1f}",
		volume_of_shape(s1), volume_of_shape(s2));

	double vol_common = -1, vol_left = -1, vol_right = -1;
	const auto result = classify_solid_intersection(
		s1, s2, 0.5, vol_common, vol_left, vol_right);

	spdlog::info(
		"classification = {}, vols = {:.1f} {:.1f} {:.1f}",
		result, vol_common, vol_left, vol_right);
}

int
main()
{
	test_bops();

	return 0;
}
