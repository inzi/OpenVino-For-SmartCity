#include "Tracker.h"

#include <dlib/image_processing.h>
#include <dlib/gui_widgets.h>
#include <dlib/image_io.h>
#include <dlib/dir_nav.h>
#include <dlib/opencv.h>

/* ==========================================================================

Class : Util

Many useful but not fundamental functions are implemented in this class.
All functions are static functions so don't need to make class Util object
to use these functions.

========================================================================== */
class Util
{
public:

	/* --------------------------------------------
	Function : cvtRectToRect
	Convert cv::Rect to dlib::drectangle
	----------------------------------------------- */
	static dlib::drectangle cvtRectToDrect(cv::Rect _rect)
	{
		return dlib::drectangle(_rect.tl().x, _rect.tl().y, _rect.br().x - 1, _rect.br().y - 1);
	}


	/* -------------------------------------------------
	Function : cvtMatToArray2d
	convert cv::Mat to dlib::array2d<unsigned char>
	------------------------------------------------- */
	static dlib::array2d<unsigned char> cvtMatToArray2d(cv::Mat _mat) // cv::Mat, not cv::Mat&. Make sure use copy of image, not the original one when converting to grayscale
	{

		//Don't need to use color image in HOG-feature-based tracker
		//Convert color image to grayscale
		if (_mat.channels() == 3)
			cv::cvtColor(_mat, _mat, cv::COLOR_RGB2GRAY);

		//Convert opencv 'MAT' to dlib 'array2d<unsigned char>'
		dlib::array2d<unsigned char> dlib_img;
		dlib::assign_image(dlib_img, dlib::cv_image<unsigned char>(_mat));

		return dlib_img;
	}


	/* -----------------------------------------------------------------
	Function : setRectToImage
	Put all tracking results(new rectangle) on the frame image
	Parameter _rects is stl container(such as vector..) filled with
	cv::Rect
	----------------------------------------------------------------- */
	template <typename Container>
	static void setRectToImage(cv::Mat& _mat_img, Container _rects)
	{
		std::for_each(_rects.begin(), _rects.end(), [&_mat_img](cv::Rect rect) {
			cv::rectangle(_mat_img, rect, cv::Scalar(0, 0, 255));
		});
	}
};

/* ---------------------------------------------------------------------------------

Function : calcVel

Calculate velocity as an average of last n_frames frames (dX, dY).

---------------------------------------------------------------------------------*/
void SingleTracker::calcVel()
{
	double delta_x = 0;
	double delta_y = 0;
	cv::Point avgvel;

	if (this->c_q.size() >= 5) {
		delta_x = (this->c_q[4].x - this->c_q[0].x)*5;
		delta_y = (this->c_q[4].y - this->c_q[0].y)*5;
	}
	avgvel = ((this->getVel() - this->getCenter()) + cv::Point(std::round(delta_x),std::round(delta_y)))/2;
	this->setVel(this->getCenter() + avgvel);
}

/* ---------------------------------------------------------------------------------

Function : startSingleTracking

Initialize dlib::correlation_tracker tracker using dlib::start_track function

---------------------------------------------------------------------------------*/
int SingleTracker::startSingleTracking(cv::Mat _mat_img)
{
	// Exception
	if (_mat_img.empty())
	{
		std::cout << "====================== Error Occured! =======================" << std::endl;
		std::cout << "Function : int SingleTracker::startSingleTracking" << std::endl;
		std::cout << "Parameter cv::Mat& _mat_img is empty image!" << std::endl;
		std::cout << "=============================================================" << std::endl;

		return FAIL;
	}

	// Convert _mat_img to dlib::array2d<unsigned char>
	dlib::array2d<unsigned char> dlib_frame = Util::cvtMatToArray2d(_mat_img);

	// Convert SingleTracker::rect to dlib::drectangle
	dlib::drectangle dlib_rect = Util::cvtRectToDrect(this->getRect());

	// Initialize SingleTracker::tracker
	this->tracker.start_track(dlib_frame, dlib_rect);
	this->setIsTrackingStarted(true);

	return SUCCESS;
}

/*---------------------------------------------------------------------------------

Function : isTargetInsideFrame

Check the target is inside the frame
If the target is going out of the frame, need to SingleTracker stop that target.

---------------------------------------------------------------------------------*/
int SingleTracker::isTargetInsideFrame(int _frame_width, int _frame_height)
{
	int cur_x = this->getCenter().x;
	int cur_y = this->getCenter().y;

	bool is_x_inside = ((0 <= cur_x) && (cur_x < _frame_width));
	bool is_y_inside = ((0 <= cur_y) && (cur_y < _frame_height));

	if (is_x_inside && is_y_inside)
		return TRUE;
	else
		return FALSE;
}

/* -----------------------------------------------------------------------------------

Function : markForDeletion(std::vector<std::pair<cv::Rect, int>> rois)

Mark trackers to delete.

----------------------------------------------------------------------------------- */

int SingleTracker::markForDeletion()
{
	const int frames = 12; // Arbitrary numbers, adjust if needed
	const double min_vel = 0.01*this->rect.area();

	if (this->no_update_counter >= frames && this->modvel < min_vel)
		this->to_delete = true;

	return SUCCESS;
}

/* ---------------------------------------------------------------------------------

Function : doSingleTracking

Track 'one' target specified by SingleTracker::rect in a frame.
(It means that you need to call doSingleTracking once per a frame)
SingleTracker::rect is initialized to the target position in the constructor of SingleTracker
Using correlation_tracker in dlib, start tracking 'one' target

--------------------------------------------------------------------------------- */
int SingleTracker::doSingleTracking(cv::Mat _mat_img)
{
	//Exception
	if (_mat_img.empty())
	{
		std::cout << "====================== Error Occured! ======================= " << std::endl;
		std::cout << "Function : int SingleTracker::doSingleTracking" << std::endl;
		std::cout << "Parameter cv::Mat& _mat_img is empty image!" << std::endl;
		std::cout << "=============================================================" << std::endl;

		return FAIL;
	}

	// Convert _mat_img to dlib::array2d<unsigned char>
	dlib::array2d<unsigned char> dlib_img = Util::cvtMatToArray2d(_mat_img);

	// Track using dlib::update function
	if (this->getUpdateFromDetection()) {
		dlib::drectangle dlib_rect = Util::cvtRectToDrect(this->getRect());
		this->tracker.start_track(dlib_img, dlib_rect);
		this->setUpdateFromDetection(false);
	} else {
		double confidence = this->tracker.update_noscale(dlib_img);
	}

	// New position of the target
	dlib::drectangle updated_rect = this->tracker.get_position();

	// Update variables(center, rect, confidence)
	this->setCenter(updated_rect);
	this->setRect(updated_rect);
	this->setConfidence(confidence);
	this->saveLastCenter(this->getCenter());
	this->calcVel();
	this->no_update_counter++;
	this->markForDeletion();
	return SUCCESS;
}

/* -------------------------------------------------------------------------

Function : insertTracker

Create new SingleTracker object and insert it to the vector.
If you are about to track new person, need to use this function.

------------------------------------------------------------------------- */

int TrackerManager::insertTracker(cv::Rect _init_rect, cv::Scalar _color, int _target_id, int _label, bool update, std::string *last_event)
{
	// Exceptions
	if (_init_rect.area() == 0)
	{
		std::cout << "======================= Error Occured! ====================== " << std::endl;
		std::cout << "Function : int SingleTracker::initTracker" << std::endl;
		std::cout << "Parameter cv::Rect _init_rect's area is 0" << std::endl;
		std::cout << "=============================================================" << std::endl;

		return FAIL;
	}

	// if _target_id already exists
	int result_idx = findTrackerByID(_target_id);
	// Create new SingleTracker object and insert it to the vector
	std::shared_ptr<SingleTracker> new_tracker(new SingleTracker(_target_id, _init_rect, _color, _label));

	if (result_idx != FAIL)	{
		if (!update) {
			std::cout << "======================= Error Occured! ======================" << std::endl;
			std::cout << "Function : int SingleTracker::initTracker" << std::endl;
			std::cout << "_target_id already exists!" << std::endl;
			std::cout << "=============================================================" << std::endl;

			return FAIL;
		} else {
			this->tracker_vec[result_idx]->setCenter(new_tracker->getCenter());
			this->tracker_vec[result_idx]->setRect(_init_rect);
			this->tracker_vec[result_idx]->setUpdateFromDetection(update);
			this->tracker_vec[result_idx]->setNoUpdateCounter(0);
			this->tracker_vec[result_idx]->setLabel(_label);
			this->tracker_vec[result_idx]->setColor(_color);
		}
	} else {
		this->tracker_vec.push_back(new_tracker);
		this->id_list = _target_id + 1; // Next ID
		std::stringstream aux_str;

		aux_str << "========================== Notice! ==========================" << std::endl;
		aux_str << "Target ID : " << this->id_list-1 << " is now been tracked" << std::endl;
		aux_str << "=============================================================" << std::endl;

		*last_event = aux_str.str();
	}

	return SUCCESS;
}

// Overload of insertTracker
int TrackerManager::insertTracker(std::shared_ptr<SingleTracker> new_single_tracker, bool update)
{
	//Exception
	if (new_single_tracker == nullptr)
	{
		std::cout << "======================== Error Occured! ===================== " << std::endl;
		std::cout << "Function : int TrackerManager::insertTracker" << std::endl;
		std::cout << "Parameter shared_ptr<SingleTracker> new_single_tracker is nullptr" << std::endl;
		std::cout << "=============================================================" << std::endl;

		return FAIL;
	}

	// if _target_id already exists
	int result_idx = findTrackerByID(new_single_tracker.get()->getTargetID());
	if (result_idx != FAIL) {
		if (!update) {
			std::cout << "====================== Error Occured! =======================" << std::endl;
			std::cout << "Function : int SingleTracker::insertTracker" << std::endl;
			std::cout << "_target_id already exists!" << std::endl;
			std::cout << "=============================================================" << std::endl;

			return FAIL;
		} else {
			this->tracker_vec[result_idx]->setCenter(new_single_tracker->getCenter());
			this->tracker_vec[result_idx]->setRect(new_single_tracker->getRect());
			this->tracker_vec[result_idx]->setUpdateFromDetection(update);
			this->tracker_vec[result_idx]->setNoUpdateCounter(0);
		}
	} else {
		// Insert new SingleTracker object into the vector
		this->tracker_vec.push_back(new_single_tracker);
		this->id_list = new_single_tracker.get()->getTargetID() + 1; //Next ID

	}

	return SUCCESS;
}

/* -----------------------------------------------------------------------------------

Function : findTrackerByID

Find SingleTracker object which has ID : _target_id in the TrackerManager::tracker_vec
If success to find return that iterator, or return TrackerManager::tracker_vec.end()

----------------------------------------------------------------------------------- */
int TrackerManager::findTrackerByID(int _target_id)
{
	auto target = find_if(tracker_vec.begin(), tracker_vec.end(), [&, _target_id](std::shared_ptr<SingleTracker> ptr) -> bool {
		return (ptr.get() -> getTargetID() == _target_id);
	});

	if (target == tracker_vec.end())
		return FAIL;
	else
		return target - tracker_vec.begin();
}

/* -----------------------------------------------------------------------------------

Function : findTracker

Find SingleTracker object in the TrackerManager::tracker_vec
If success to find return that index, or return new index if no coincidence

----------------------------------------------------------------------------------- */
int TrackerManager::findTracker(cv::Rect rect, int label)
{
	double max_overlap_thresh = 0.9;
	double dist_thresh = rect.height*rect.width>>1; // Pixels^2 -> adjust properly (maybe a proportion of the img size?)
	std::vector<std::shared_ptr<SingleTracker>> selection;
	std::shared_ptr<SingleTracker> best = NULL;
	double min_distance = (rect.height*rect.width)+10; // Init bigger than threshold
	int index;
	bool new_object = true;
	std::vector<double> areas;

	for(auto && s_tracker: this->getTrackerVec()) {
		double in_area = (s_tracker.get()->getRect() & rect).area();
		double max_per_area = std::max(in_area / s_tracker.get()->getRect().area(), in_area/rect.area());
		areas.push_back(max_per_area);
		if ( max_per_area > max_overlap_thresh && (s_tracker->getLabel() == label || s_tracker->getLabel() == LABEL_UNKNOWN) ) {
			selection.push_back(s_tracker);
		}
	}

	for (auto && s_tracker: selection) {
		cv::Point n_center = cv::Point(rect.x + (rect.width) / 2, rect.y + (rect.height) / 2);
		cv::Point diff = s_tracker.get()->getCenter() - n_center;
		double distance = diff.x*diff.x + diff.y*diff.y;
		if (best == NULL && distance < dist_thresh) {
			min_distance = distance;
			best = s_tracker;
		} else if ( best != NULL && distance < min_distance ) {
			min_distance = distance;
			best = s_tracker;
		}
	}

	for (auto && area: areas) {
		if (area != 0.0) {
			new_object = false;
			break;
		}
	}

	if ( best == NULL && new_object ) {
		index = this->getNextID();
	} else if ( best != NULL ) {
		index = best.get()->getTargetID();
	} else if (!new_object) {
		index = -1;
	}

	return index;
}

/* -----------------------------------------------------------------------------------

Function : deleteTracker

Delete SingleTracker object which has ID : _target_id in the TrackerManager::tracker_vec

----------------------------------------------------------------------------------- */
int TrackerManager::deleteTracker(int _target_id, std::string *last_event)
{
	int result_idx = this->findTrackerByID(_target_id);

	if (result_idx == FAIL)
	{
		std::cout << "======================== Error Occured! =====================" << std::endl;
		std::cout << "Function : int TrackerManager::deleteTracker" << std::endl;
		std::cout << "Cannot find given _target_id" << std::endl;
		std::cout << "=============================================================" << std::endl;

		return FAIL;
	}
	else
	{
		// Memory deallocation
		this->tracker_vec[result_idx].reset();

		// Remove SingleTracker object from the vector
		this->tracker_vec.erase(tracker_vec.begin() + result_idx);

		std::stringstream aux_str;

		aux_str << "========================== Notice! ==========================" << std::endl;
		aux_str << "Target ID : " << _target_id << " is going out of the frame." << std::endl;
		aux_str << "Target ID : " << _target_id << " is erased!" << std::endl;
		aux_str << "=============================================================" << std::endl;

		*last_event = aux_str.str();
		return SUCCESS;
	}
}

/* -----------------------------------------------------------------------------------

Function : initTrackingSystem()

Insert multiple SingleTracker objects to the manager.tracker_vec in once.
If you want multi-object tracking, call this function just for once like

vector<cv::Rect> rects;
// Insert all rects into the vector

vector<int> ids;
// Insert all target_ids into the vector

initTrackingSystem(ids, rects)

Then, the system is ready to track multiple targets.

----------------------------------------------------------------------------------- */

int TrackingSystem::initTrackingSystem()
{
	int index = 0;
	cv::Scalar color = COLOR_UNKNOWN;
	int label = LABEL_UNKNOWN;

	for( auto && i : this->init_target){
		if (i.second == LABEL_CAR) {
			color = COLOR_CAR;
			label = LABEL_CAR;
		}
		else if (i.second == LABEL_PERSON) {
			color = COLOR_PERSON;
			label = LABEL_PERSON;
		}

		if (this->manager.insertTracker(i.first, color, index, label, false, this->last_event) == FAIL)
		{
			std::cout << "====================== Error Occured! =======================" << std::endl;
			std::cout << "Function : int TrackingSystem::initTrackingSystem" << std::endl;
			std::cout << "Cannot insert new SingleTracker object to the vector" << std::endl;
			std::cout << "=============================================================" << std::endl;
			return FAIL;
		}
		index++;
	}
	return SUCCESS;
}

/* -----------------------------------------------------------------------------------

Function : updateTrackingSystem(std::vector<std::pair<cv::Rect, int>> rois)

Insert new multiple SingleTracker objects to the manager.tracker_vec.
If you want multi-object tracking, call this function just for once like

vector<cv::Rect> rects;
// Insert all rects into the vector

vector<int> ids;
// Insert all target_ids into the vector

initTrackingSystem(ids, rects)

Then, the system is ready to track new targets.

----------------------------------------------------------------------------------- */

int TrackingSystem::updateTrackingSystem(std::vector<std::pair<cv::Rect, int>> updated_results)
{
	cv::Scalar color = COLOR_UNKNOWN;
	int label = LABEL_UNKNOWN;

	//Update init_target to detect new objects
	//this->updated_target = updated_results;

	for( auto && i : updated_results){
		int index;
		if (i.second == LABEL_CAR) {
			color = COLOR_CAR;
			label = LABEL_CAR;
		}
		else if (i.second == LABEL_PERSON) {
			color = COLOR_PERSON;
			label = LABEL_PERSON;
		}
		index = this->manager.findTracker(i.first, label);
		if ( index != -1) {
			if (this->manager.insertTracker(i.first, color, index, label, true,this->last_event) == FAIL)
			{
				std::cout << "====================== Error Occured! =======================" << std::endl;
				std::cout << "Function : int TrackingSystem::updateTrackingSystem" << std::endl;
				std::cout << "Sth went wrong" << std::endl;
				std::cout << "=============================================================" << std::endl;
				return FAIL;
			}
		}
	}
	return SUCCESS;
}

/* -----------------------------------------------------------------------------------

Function : startTracking()

Track all targets.
You don't need to give target id for tracking.
This function will track all targets.

----------------------------------------------------------------------------------- */
int TrackingSystem::startTracking(cv::Mat& _mat_img)
{
	// Check the image is empty
	if (_mat_img.empty())
	{
		std::cout << "======================= Error Occured! ======================" << std::endl;
		std::cout << "Function : int TrackingSystem::startTracking" << std::endl;
		std::cout << "Input image is empty" << std::endl;
		std::cout << "=============================================================" << std::endl;
		return FAIL;
	}

	// Convert _mat_img to dlib::array2d<unsigned char>
	dlib::array2d<unsigned char> dlib_cur_frame = Util::cvtMatToArray2d(_mat_img);

	// For all SingleTracker, do SingleTracker::startSingleTracking.
	// Function startSingleTracking should be done before doSingleTracking
	std::for_each(manager.getTrackerVec().begin(), manager.getTrackerVec().end(), [&](std::shared_ptr<SingleTracker> ptr) {
		if (!(ptr.get()->getIsTrackingStarted()))
		{
			ptr.get()->startSingleTracking(_mat_img);
			ptr.get()->setIsTrackingStarted(true);
		}
	});

	std::vector<std::thread> thread_pool;

	// Multi thread
	std::for_each(manager.getTrackerVec().begin(), manager.getTrackerVec().end(), [&](std::shared_ptr<SingleTracker> ptr) {
		thread_pool.emplace_back([ptr, &_mat_img]() { 
		 ptr.get()->doSingleTracking(_mat_img); 
		});
	});

	for (int i = 0; i < thread_pool.size(); i++)
		thread_pool[i].join();

	// If target is going out of the frame, delete that tracker.
	std::vector<int> tracker_erase;
	for(auto && i: manager.getTrackerVec()){
		if (i->isTargetInsideFrame(this->getFrameWidth(), this->getFrameHeight()) == FALSE || i->getDelete())
		{
			int target_id = i.get()->getTargetID();
			tracker_erase.push_back(target_id);
		}
	}

	for(auto && i : tracker_erase){
		int a = manager.deleteTracker(i,this->last_event);
	}

	return SUCCESS;
}

/* -----------------------------------------------------------------------------------

Function : drawTrackingResult

Deallocate all memory and close the program.

----------------------------------------------------------------------------------- */
int TrackingSystem::drawTrackingResult(cv::Mat& _mat_img)
{
	TrackerManager manager = this->getTrackerManager();

	// Exception
	if (manager.getTrackerVec().size() == 0)
	{
		std::cout << "======================= Error Occured! ======================" << std::endl;
		std::cout << "Function : int TrackingSystem::drawTrackingResult" << std::endl;
		std::cout << "Nothing to draw" << std::endl;
		std::cout << "=============================================================" << std::endl;
		return FAIL;
	}

	std::for_each(manager.getTrackerVec().begin(), manager.getTrackerVec().end(), [&_mat_img](std::shared_ptr<SingleTracker> ptr) {
		// Draw all rectangles
		cv::rectangle(_mat_img, ptr.get()->getRect(), ptr.get()->getColor(), 1);
		// Draw velocities
		cv::arrowedLine(_mat_img, ptr.get()->getCenter(), ptr.get()->getVel(), ptr.get()->getColor(), 1);
		// Draw trajectories
		boost::circular_buffer<cv::Point> centers = ptr.get()->getCenters_q();
		for (int i=0; i<(centers.size()-1); ++i) {
			cv::line(_mat_img, centers.at(i+1), centers.at(i), ptr.get()->getColor(), 1);
		}
		std::string str_label;

		switch (ptr.get()->getLabel()) {
		case LABEL_CAR:
			str_label = "Car";
			break;
		case LABEL_PERSON:
			str_label = "Person";
			break;
		default:
			str_label = "Unknown";
			break;
		}
		cv::String text(std::string("ID: ") + std::to_string(ptr.get()->getTargetID()) + " Class: " + str_label);
		cv::Point text_pos = ptr.get()->getRect().tl();
		text_pos.x = text_pos.x - 10;
		text_pos.y = text_pos.y - 5;

		// Put all target ids
		cv::putText(_mat_img,
			text,
			text_pos,
			cv::FONT_HERSHEY_SIMPLEX,
			0.5, //Scale
			ptr.get()->getColor(),
			1); //Width
	});

	return SUCCESS;
}

/* -----------------------------------------------------------------------------------

Function : isValidCollision

Helper function for detectCollisions. A collision is valid if the ratios and threshold
between the different objects is the right one. If not, it just means that they get
occluded but they don't really collide.

----------------------------------------------------------------------------------- */
bool isValidCollision(std::pair<double, int> area1, std::pair<double, int> area2)
{
	const double ratioPtoC = 31;
	const double ratioPtoB = 6.9;
	const double ratioBtoC = 4.5;
	const double threshold = 0.2;

	double a1 = area1.first;
	int label1 = area1.second;
	double a2 = area2.first;
	int label2 = area2.second;

	if (label1 == LABEL_UNKNOWN || label2 == LABEL_UNKNOWN) {
		return FALSE;
	}

	if (label1 != label2) {
		if (label1 == LABEL_CAR) {
			std::swap(label1, label2);
			std::swap(area1, area2);
		}
		//Extend to further cases if needed
	}

	if (label1 == LABEL_PERSON) {
		if (label2 == LABEL_PERSON) {
			if (a1 > a2*(1-threshold) && a1 < a2*(1+threshold)) {
				return TRUE;
			}
		} else if (label2 == LABEL_CAR) {
			if (a1*ratioPtoC > a2*(1-threshold) && a1*ratioPtoC < a2*(1+threshold)) {
				return TRUE;
			}
		}
	} else if (label1 == LABEL_CAR) {
		if (a1 > a2*(1-threshold) && a1 < a2*(1+threshold)) {
			return TRUE;
		}
	}

	return FALSE;
}


/* -----------------------------------------------------------------------------------

Function : detectCollisions

Draw red circle when collision is detected and write to log.

----------------------------------------------------------------------------------- */
int TrackingSystem::detectCollisions(cv::Mat& _mat_img)
{
	TrackerManager manager = this->getTrackerManager();

	// Exception
	if (manager.getTrackerVec().size() == 0)
	{
		std::cout << "======================= Error Occured! ======================" << std::endl;
		std::cout << "Function : int TrackingSystem::detectCollisions" << std::endl;
		std::cout << "Nothing to detect" << std::endl;
		std::cout << "=============================================================" << std::endl;
		return FAIL;
	}

	std::vector<std::shared_ptr<SingleTracker>> trackerVec = manager.getTrackerVec();
	for (auto i = trackerVec.begin(); i != trackerVec.end(); ++i) {
		SingleTracker iRef = *(*i);
		for (auto j = i + 1; j != trackerVec.end(); ++j) {
		SingleTracker jRef = *(*j);
		cv::Rect recti = iRef.getRect();
		cv::Rect rectj = jRef.getRect();
		bool intersects = ((recti & rectj).area() > 0);
		if (intersects && isValidCollision(std::make_pair(recti.area(),iRef.getLabel()),std::make_pair(rectj.area(),jRef.getLabel()))) {
			std::cout<<"Collision between object "<<iRef.getTargetID()<<" and "<<jRef.getTargetID()<<std::endl;
			cv::circle(_mat_img,
					   (iRef.getCenter() + jRef.getCenter())*.5,
					   10, //radius
					   cv::Scalar(0,0,255),
					   3); //width
			}
		}
	}

	return SUCCESS;
}

/* -----------------------------------------------------------------------------------

Function : terminateSystem

Draw rectangle around the each target and put target id on rectangle.

----------------------------------------------------------------------------------- */
void TrackingSystem::terminateSystem()
{
	std::vector<std::shared_ptr<SingleTracker>> remaining_tracker = manager.getTrackerVec();

	// Memory deallocation
	std::for_each(remaining_tracker.begin(), remaining_tracker.end(),
		[](std::shared_ptr<SingleTracker> ptr) { ptr.reset(); });

	std::cout << "Close Tracking System..." << std::endl;
}
