#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>

#include <BRepBndLib.hxx>
#include <Bnd_OBB.hxx>

#include <cxx_argp_parser.h>
#include <aixlog.hpp>

#include "document.hpp"
#include "utils.hpp"


// helper methods to allow fmt to display some OCCT values nicely
static std::ostream& operator<<(std::ostream& str, Bnd_OBB obb) {
	str << '{';
	obb.DumpJson(str);
	str << '}';
	return str;
}

struct worker_input {
	size_t hi, lo;
};

struct worker_output {
	size_t hi, lo;
	intersect_result result;
};

class worker_queue {
	std::mutex mutex;
	std::condition_variable cond;

	std::deque<worker_input> input;
	std::deque<worker_output> output;

public:
	size_t input_size() {
		return input.size();
	}

	void add_work(const worker_input &work) {
		input.push_back(work);
	}

	bool next_input(worker_input &work) {
		std::unique_lock<std::mutex> mlock(mutex);

		if (input.empty()) {
			return false;
		}

		work = input.front();
		input.pop_front();

		return true;
	}

	void add_output(const worker_output &work) {
		std::unique_lock<std::mutex> mlock(mutex);

		output.push_back(work);
		mlock.unlock();

		cond.notify_one();
	}

	worker_output next_output() {
		std::unique_lock<std::mutex> mlock(mutex);
		while (output.empty()) {
			cond.wait(mlock);
		}
		auto result = output.front();
		output.pop_front();
		return result;
	}
};

static void
shape_classifier(
	const document &doc, worker_queue &queue, std::vector<double> &fuzzy_values)
{
	LOG(DEBUG) << "worker thread starting\n";

	const auto &shapes = doc.solid_shapes;

	worker_input input;
	worker_output output;

	while (queue.next_input(input)) {
		const auto &shape = shapes[output.hi = input.hi];
		const auto &tool = shapes[output.lo = input.lo];

		bool first = true;
		for (const auto fuzzy_value : fuzzy_values) {
			if (!first) {
				LOG(INFO)
					<< std::setw(5) << input.hi << '-' << std::left << input.lo << std::right
					<< " imprint failed with "
					<< '(' << output.result.num_filler_warnings << " filler and "
					<< output.result.num_common_warnings << " common) "
					<< "warnings, retrying with tolerance=" << fuzzy_value << '\n';
			}

			output.result = classify_solid_intersection(
				shape, tool, fuzzy_value);

			// try again with less fuzz
			if (output.result.status != intersect_status::failed) {
				break;
			}
		}

		if (output.result.status == intersect_status::failed) {
			LOG(WARNING)
				<< std::setw(5) << input.hi << '-' << std::left << input.lo << std::right
				<< " imprint failed with "
				<< '(' << output.result.num_filler_warnings << " filler and "
				<< output.result.num_common_warnings << " common) "
				<< "warnings\n";
		}

		queue.add_output(output);
	}

	LOG(DEBUG) << "worker thread exiting\n";
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
		const char * doc = "Perform imprinting of BREP shapes.";
		const char * usage = "input.brep";


		std::stringstream stream;

		stream << "Parallelise over N[=" << num_parallel_jobs << "] threads";
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

		cxx_argp::parser argp(1);
		argp.add_option(
			{"jobs", 'j', "N", 0, help_num_par_jobs.c_str()}, num_parallel_jobs);
		argp.add_option(
			{"bbox-clearance", 1024, "C", 0, help_bbox_cl.c_str()}, bbox_clearance);
		argp.add_option(
			{"imprint-tolerance", 1025, "T", 0, help_imp_tol.c_str()}, imprint_tolerances);
		argp.add_option(
			{"max-common-volume-ratio", 1026, "R", 0, help_max_common.c_str()}, max_common_volume_ratio);

		if (!argp.parse(argc, argv, usage, doc)) {
			return 1;
		}

		const auto &args = argp.arguments();
		assert(args.size() == 1);
		path_in = args[0];

		if (num_parallel_jobs < 1 || num_parallel_jobs > 1024) {
			LOG(ERROR)
				<< "Number of parallel jobs must be between 1 and 1024.\n";
			return 1;
		}
		unsigned max_parallel = std::thread::hardware_concurrency();
		if (max_parallel && num_parallel_jobs > max_parallel * 2 + 4) {
			LOG(WARNING)
				<< "Requesting significantly more than the number of cores "
				<< '(' << num_parallel_jobs << " > " << max_parallel << ") is unlikely to help\n";
		}

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

	LOG(DEBUG) << "calculating bounding boxes\n";
	std::vector<Bnd_OBB> bounding_boxes;
	std::vector<double> volumes;
	bounding_boxes.reserve(doc.solid_shapes.size());
	volumes.reserve(doc.solid_shapes.size());
	for (const auto &shape : doc.solid_shapes) {
		Bnd_OBB obb;
		BRepBndLib::AddOBB(shape, obb);
		bounding_boxes.push_back(obb);

		volumes.push_back(volume_of_shape(shape));
	}

	LOG(DEBUG) << "starting imprinting\n";

	worker_queue queue;

	for (size_t hi = 1; hi < doc.solid_shapes.size(); hi++) {
		for (size_t lo = 0; lo < hi; lo++) {
			// seems reasonable to assume majority of shapes aren't close to
			// overlapping, so check with coarser limit first
			if (are_bboxs_disjoint(
					bounding_boxes[hi], bounding_boxes[lo], bbox_clearance)) {
				continue;
			}

			worker_input work{hi, lo};
			queue.add_work(work);
		}
	}

	{
		size_t
			remain = queue.input_size(),
			num_failed = 0,
			num_intersected = 0;

		LOG(DEBUG) << "launching " << num_parallel_jobs << " worker threads\n";

		std::vector<std::thread> threads;
		for (unsigned i = 0; i < num_parallel_jobs; i++) {
			threads.emplace_back(shape_classifier, std::ref(doc), std::ref(queue), std::ref(imprint_tolerances));
		}

		LOG(DEBUG) << "waiting for results from workers\n";

		while (remain--) {
			worker_output output = queue.next_output();

			size_t hi = output.hi, lo = output.lo;

			std::stringstream hi_lo;
			hi_lo << std::setw(5) << hi << '-' << std::left << lo << std::right;

			switch (output.result.status) {
			case intersect_status::failed:
				LOG(ERROR) << hi_lo.str() << " failed to classify overlap\n";
				num_failed += 1;
				break;
			case intersect_status::distinct:
				LOG(DEBUG) << hi_lo.str() << " are distinct\n";
				break;
			case intersect_status::touching:
				std::cout << hi << ',' << lo << ",touch\n";
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
						<< hi_lo.str() << " overlap by more than " << overlap_msg.str();
					std::cout << hi << ',' << lo << ",bad_overlap\n";
					num_intersected += 1;
				} else {
					LOG(INFO)
						<< hi_lo.str() << " overlap by less than " << overlap_msg.str();
					std::cout << hi << ',' << lo << ",overlap\n";
				}
				break;
			}
			}

			// flush any CSV output
			std::cout << std::flush;
		}

		LOG(DEBUG) << "joining worker threads\n";
		for (auto &thread : threads) {
			thread.join();
		}

		if (num_failed || num_intersected) {
			LOG(ERROR)
				<< "errors occurred while processing, "
				<< num_failed << " failed, "
				<< num_intersected << " intersected\n";
			return 1;
		}
	}

	return 0;
}
