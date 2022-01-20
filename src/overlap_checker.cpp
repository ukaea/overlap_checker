#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <BRepBndLib.hxx>
#include <Bnd_OBB.hxx>

#include <cxx_argp_parser.h>
#include <aixlog.hpp>

#include "geometry.hpp"
#include "utils.hpp"
#include "thread_pool.hpp"


struct worker_state {
	const document &doc;
	std::vector<double> &fuzzy_values;
};

struct worker_output {
	size_t hi, lo;
	intersect_result result;
};

static worker_output
shape_classifier(const worker_state& state, size_t hi, size_t lo)
{
	const auto &shape = state.doc.solid_shapes[hi];
	const auto &tool = state.doc.solid_shapes[lo];

	intersect_result result;

	bool first = true;
	for (const auto fuzzy_value : state.fuzzy_values) {
		if (!first) {
			LOG(INFO)
				<< indexpair_to_string(hi, lo) << " imprint failed with "
				<< '(' << result.num_filler_warnings << " filler and "
				<< result.num_common_warnings << " common) "
				<< "warnings, retrying with tolerance=" << fuzzy_value << '\n';
		}

		result = classify_solid_intersection(
			shape, tool, fuzzy_value);

		// try again with less fuzz
		if (result.status != intersect_status::failed) {
			break;
		}
	}

	if (result.status == intersect_status::failed) {
		LOG(WARNING)
			<< indexpair_to_string(hi, lo) << " imprint failed with "
			<< '(' << result.num_filler_warnings << " filler and "
			<< result.num_common_warnings << " common) "
			<< "warnings\n";
	}

	return {hi, lo, result};
}


// "OBB" stands for "orientated bounding-box", i.e. aligned to the shape
// rather than the axis
static bool
are_bboxs_disjoint(const Bnd_OBB &b1, const Bnd_OBB& b2, double tolerance)
{
	if (tolerance > 0) {
		Bnd_OBB e1{b1}, e2{b2};
		e1.Enlarge(tolerance);
		e2.Enlarge(tolerance);
		return e1.IsOut(e2);
	}
	return b1.IsOut(b2);
}

int
main(int argc, char **argv)
{
	configure_aixlog();

	std::string path_in;
	unsigned num_parallel_jobs = 1;
	double
		bbox_clearance = 0.5,
		max_common_volume_ratio = 0.01;
	std::vector<double> imprint_tolerances = {0.001, 0};

	{
		const char * doc = (
			"Find all pairwise intersections between solids.\n"
			"\n"
			"Will output a CSV file to stdout containing a row "
			"for each pair of nearby shapes categorised as:"
			"'touch' when edges or verticies intersect, "
			"'overlap' when shapes overlap < the common volume ratio, and "
			"'bad_overlap' when they overlap by more.");
		const char * usage = "input.brep";

		std::stringstream stream;

		stream
			<< "Parallelise over N[=" << num_parallel_jobs << "] threads, leave N blank "
			"to use all (" << std::thread::hardware_concurrency() << ") cores.";
		auto help_num_par_jobs = stream.str();
		stream = {};

		stream << "Bounding-boxes closer than C[=" << bbox_clearance << "] will be checked for overlaps";
		auto help_bbox_cl = stream.str();
		stream = {};

		stream
			<< "Faces, edges, and verticies will be merged when closer than T[="
			<< imprint_tolerances[0] << ']';
		auto help_imp_tol = stream.str();
		stream = {};

		stream
			<< "Imprinted volume with ratio <R[="
			<< max_common_volume_ratio << "] is considered acceptable";
		auto help_max_common = stream.str();
		stream = {};

		auto parse_parallel = [&num_parallel_jobs](int, const char* arg, struct argp_state* state) {
			if (!arg) {
				num_parallel_jobs = std::thread::hardware_concurrency();
				LOG(DEBUG)
					<< "Using " << num_parallel_jobs << " threads for parallel computation\n";
				return true;
			}

			// sanity checking user input
			const long parallel_job_limit = 9999;
			try {
				size_t end;
				auto n = std::stol(arg, &end);
				if (arg[end] != '\0') {
					argp_error(
						state, "trailing characters after number in '%s'",
						arg);
					return false;
				}
				// make sure the user isn't doing anything silly
				if (n < 1 || n > parallel_job_limit) {
					argp_error(
						state, "number of parallel jobs should be between 1 and %li, not %li",
						parallel_job_limit, n);
					return false;
				}
				num_parallel_jobs = unsigned(n);
				return true;
			} catch(std::exception &err) {
				argp_error(
					state, "not a valid number: '%s'",
					arg);
				return false;
			}
		};

		tool_argp_parser argp(1);
		argp.add_option(
			{"jobs", 'j', "N", OPTION_ARG_OPTIONAL, help_num_par_jobs.c_str(), 0},
			std::function{parse_parallel});
		argp.add_option(
			{"bbox-clearance", 1024, "C", 0, help_bbox_cl.c_str(), 0}, bbox_clearance);
		argp.add_option(
			{"imprint-tolerance", 1025, "T", 0, help_imp_tol.c_str(), 0}, imprint_tolerances);
		argp.add_option(
			{"max-common-volume-ratio", 1026, "R", 0, help_max_common.c_str(), 0}, max_common_volume_ratio);

		if (!argp.parse(argc, argv, usage, doc)) {
			return 1;
		}

		const auto &args = argp.arguments();
		assert(args.size() == 1);
		path_in = args[0];

		for (const auto tolerance : imprint_tolerances) {
			if (tolerance < 0) {
				LOG(ERROR)
					<< "Imprinting tolerance should not be negative, "
					<< tolerance << " < 0\n";
				return 1;
			}

			if (bbox_clearance < tolerance) {
				LOG(WARNING)
					<< "Bounding-box clearance smaller than imprinting tolerance, "
					<< bbox_clearance << " < " << tolerance << '\n';
			}
		}

		if (!(max_common_volume_ratio >= 0 && max_common_volume_ratio <= 1)) {
			LOG(ERROR)
				<< "Maximum common volume ratio should be in (0, 1).\n";
			return 1;
		}
	}

	document doc;
	doc.load_brep_file(path_in.c_str());

	LOG(DEBUG) << "launching " << num_parallel_jobs << " worker threads\n";
	thread_pool pool(num_parallel_jobs);

	LOG(INFO) << "calculating " << doc.solid_shapes.size() << " bounding boxes\n";

	std::vector<Bnd_OBB> bounding_boxes(doc.solid_shapes.size());
	std::vector<double> volumes(doc.solid_shapes.size());

	{
		parfor work;
		size_t i = 0;
		for (const auto &shape : doc.solid_shapes) {
			work.submit(pool, [&bounding_boxes, &volumes, i, &shape]() {
				BRepBndLib::AddOBB(shape, bounding_boxes[i]);

				volumes[i] = volume_of_shape(shape);
			});
			i += 1;
		}
	}

	unsigned long
		num_bbox_tests = 0,
		num_to_process = 0,
		num_processed = 0,
		num_failed = 0,
		num_touching = 0,
		num_overlaps = 0,
		num_bad_overlaps = 0;

	{
		const struct worker_state state{doc, imprint_tolerances};
		asyncmap<worker_output> map;

		for (size_t hi = 1; hi < doc.solid_shapes.size(); hi++) {
			for (size_t lo = 0; lo < hi; lo++) {
				num_bbox_tests += 1;

				// seems reasonable to assume majority of shapes aren't close to
				// overlapping, so check with coarser limit first
				if (are_bboxs_disjoint(
						bounding_boxes[hi], bounding_boxes[lo], bbox_clearance)) {
					continue;
				}

				map.submit(pool, [&state, hi, lo]() {
					return shape_classifier(state, hi, lo);
				});
				num_to_process += 1;
			}
		}

		LOG(INFO) << "checking for overlaps between " << num_to_process << " pairs\n";

		const std::chrono::seconds reporting_interval{5};
		auto report_when = std::chrono::steady_clock::now() + reporting_interval;

		while (!map.empty()) {
			worker_output output = map.get();
			num_processed += 1;

			if (report_when < std::chrono::steady_clock::now()) {
				LOG(INFO)
					<< "processed " << num_processed << " pairs ("
					<< (num_processed * 100) / num_to_process << "%)\n";

				report_when += reporting_interval;
			}

			size_t hi = output.hi, lo = output.lo;
			const auto hi_lo = indexpair_to_string(hi, lo);

			switch (output.result.status) {
			case intersect_status::failed:
				LOG(ERROR) << hi_lo << " failed to classify overlap\n";
				num_failed += 1;
				break;
			case intersect_status::distinct:
				LOG(DEBUG) << hi_lo << " are distinct\n";
				break;
			case intersect_status::touching:
				std::cout << hi << ',' << lo << ",touch\n";
				num_touching += 1;
				break;
			case intersect_status::overlap: {
				const double
					vol_common = output.result.vol_common,
					min_vol = std::min(volumes[hi], volumes[lo]),
					max_overlap = min_vol * max_common_volume_ratio;

				std::stringstream overlap_msg;
				overlap_msg
					<< max_common_volume_ratio * 100 << "%, "
					<< std::fixed << std::setprecision(2) << vol_common / min_vol * 100
					<< "% of smaller shape\n";

				if (vol_common > max_overlap) {
					LOG(ERROR)
						<< hi_lo << " overlap by more than " << overlap_msg.str();
					std::cout << hi << ',' << lo << ",bad_overlap\n";
					num_bad_overlaps += 1;
				} else {
					LOG(INFO)
						<< hi_lo << " overlap by less than " << overlap_msg.str();
					std::cout << hi << ',' << lo << ",overlap\n";
					num_overlaps += 1;
				}
				break;
			}
			}

			// flush any CSV output
			std::cout << std::flush;
		}

		LOG(INFO)
			<< "processing summary: "
			<< "bbox tests=" << num_bbox_tests << ", "
			<< "intersection tests=" << num_processed << ", "
			<< "touching=" << num_touching << ", "
			<< "overlapping=" << num_overlaps << ", "
			<< "bad overlaps=" << num_bad_overlaps << ", "
			<< "tests failed=" << num_failed << '\n';

		if (num_failed || num_bad_overlaps) {
			LOG(ERROR)
				<< "errors occurred while processing: "
				<< "intersection tests failed=" << num_failed << ", "
				<< "overlapped by too much=" << num_bad_overlaps << '\n';
			return 1;
		}
	}

	return 0;
}
