#include <spdlog/spdlog.h>

#include "document.hpp"



int
main()
{
	document doc;
	doc.load_brep_file("input.brep");

	const int hi = 243, lo = 234;

	double vol_common, vol_cut, vol_cut12;
	const auto result = classify_solid_intersection(
		doc.solid_shapes[hi], doc.solid_shapes[lo], 0.1,
		vol_common, vol_cut, vol_cut12);

	spdlog::info("result = {}", result);

	return 0;
}
