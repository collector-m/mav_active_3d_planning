#define _USE_MATH_DEFINES

#include "mav_active_3d_planning/trajectory_generator.h"
#include "mav_active_3d_planning/defaults.h"

#include <mav_msgs/eigen_mav_msgs.h>

#include <random>
#include <cmath>

namespace mav_active_3d_planning {
    namespace trajectory_generators {

        // Point sampling in space, joined by linear segments
        class RandomLinear : public TrajectoryGenerator {
        public:
            // Overwrite virtual functions
            bool expandSegment(TrajectorySegment *target, std::vector<TrajectorySegment*> *new_segments);

        protected:
            friend ModuleFactory;

            RandomLinear() {}
            void setupFromParamMap(Module::ParamMap *param_map);
            bool checkParamsValid(std::string *error_message);

            // params
            double p_min_distance_;     // m
            double p_max_distance_;     // m
            double p_v_max_;            // m/s
            double p_a_max_;            // m/s2
            double p_sampling_rate_;    // Hz
            int p_n_segments_;
            int p_max_tries_;
            bool p_planar_;
            bool p_sample_yaw_;      // false: face direction of travel

            // methods
            void sampleTarget(Eigen::Vector3d *direction, double* yaw, double* decelleration_distance);
            bool buildTrajectory(const Eigen::Vector3d &start_pos, const Eigen::Vector3d &direction,
                                 double yaw, double decelleration_distance, TrajectorySegment* new_segment);
        };

//        RandomLinear::RandomLinear(double min_distance, double max_distance, double v_max, double a_max,
//                                   double sampling_rate, int n_segments, int max_tries, bool planar,
//                                   bool sample_yaw, bool collision_optimistic, double collision_radius,
//                                   std::unique_ptr<voxblox::EsdfServer> voxblox_ptr)
//                                   : p_min_distance_(min_distance),
//                                     p_max_distance_(max_distance),
//                                     p_v_max_(v_max),
//                                     p_a_max_(a_max),
//                                     p_sampling_rate_(sampling_rate),
//                                     p_n_segments_(n_segments),
//                                     p_max_tries_(max_tries),
//                                     p_planar(planar),
//                                     p_sample_yaw_(sample_yaw),
//                                     p_collision_radius_(collision_radius),
//                                     p_collision
//
//                voxblox::EsdfServer *voxblox_ptr, std::string param_ns)
//                : TrajectoryGenerator(voxblox_ptr, param_ns) {
//            assureParamsValid();
//
//        }

        void RandomLinear::setupFromParamMap(Module::ParamMap *param_map) {
            setParam<double>(param_map, "min_distance", &p_min_distance_, 1.0);
            setParam<double>(param_map, "max_distance", &p_max_distance_, 1.0);
            setParam<double>(param_map, "v_max", &p_v_max_, 1.0);
            setParam<double>(param_map, "a_max", &p_a_max_, 1.0);
            setParam<double>(param_map, "sampling_rate", &p_sampling_rate_, 20.0);
            setParam<int>(param_map, "n_segments", &p_n_segments_, 5);
            setParam<int>(param_map, "max_tries", &p_max_tries_, 1000);
            setParam<bool>(param_map, "planar", &p_planar_, true);
            setParam<bool>(param_map, "sample_yaw", &p_sample_yaw_, false);

            TrajectoryGenerator::setupFromParamMap(param_map);
        }

        bool RandomLinear::checkParamsValid(std::string *error_message) {
            if (p_max_distance_ <= 0.0 ){
                *error_message = "max_distance expected > 0.0";
                return false;
            } else if (p_max_distance_ < p_min_distance_ ){
                *error_message = "max_distance needs to be larger than min_distance";
                return false;
            } else if (p_n_segments_ < 1 ){
                *error_message = "n_segments expected > 0";
                return false;
            } else if (p_max_tries_ < 1 ){
                *error_message = "max_tries expected > 0";
                return false;
            }
            return TrajectoryGenerator::checkParamsValid(error_message);
        }

        bool RandomLinear::expandSegment(TrajectorySegment *target, std::vector<TrajectorySegment*> *new_segments) {
            // Create and add new adjacent trajectories to target segment
            target->tg_visited = true;
            int valid_segments = 0;
            int counter = 0;
            TrajectorySegment *new_segment = target->spawnChild();
            Eigen::Vector3d start_pos = target->trajectory.back().position_W;

            Eigen::Vector3d direction;
            double yaw;
            double decelleration_distance;

            while (valid_segments < p_n_segments_ && counter < p_max_tries_) {
                counter++;

                // new random target selection
                sampleTarget(&direction, &yaw, &decelleration_distance);

                // Try building the trajectory and check if it's collision free
                if (buildTrajectory(start_pos, direction, yaw, decelleration_distance, new_segment)){
                    valid_segments++;
                    new_segments->push_back(new_segment);
                    new_segment = target->spawnChild();
                }
            }
            target->children.pop_back();

            // Feasible solution found?
            return (valid_segments > 0);
        }

        void RandomLinear::sampleTarget(Eigen::Vector3d *direction, double* yaw, double* decelleration_distance) {
            // new random target direction
            *yaw = (double) rand() * 2.0 * M_PI / (double) RAND_MAX;
            double theta = (double) rand() * M_PI / (double) RAND_MAX;
            double distance = p_min_distance_ + (double) rand() / RAND_MAX * (p_max_distance_ - p_min_distance_);
            *decelleration_distance = distance - std::min(p_v_max_ * p_v_max_ / p_a_max_, distance) / 2;
            if (p_planar_) { theta = 0.5 * M_PI; }
            *direction = Eigen::Vector3d(sin(theta) * cos(*yaw), sin(theta) * sin(*yaw), cos(theta));
            if (p_sample_yaw_) {
                *yaw = (double) rand() * 2.0 * M_PI / (double) RAND_MAX; // new orientation
            }
        }

        bool RandomLinear::buildTrajectory(const Eigen::Vector3d &start_pos, const Eigen::Vector3d &direction,
                             double yaw, double decelleration_distance, TrajectorySegment* new_segment) {
            // Simulate trajectory (accelerate and deccelerate to 0 velocity at goal points)
            double x_curr = 0.0, v_curr = 0.0, t_curr = 0.0;

            while (v_curr >= 0.0) {
                if (x_curr < decelleration_distance) {
                    v_curr = std::min(v_curr + p_a_max_ / p_sampling_rate_, p_v_max_);
                } else {
                    v_curr -= p_a_max_ / p_sampling_rate_;
                }
                t_curr += 1.0 / p_sampling_rate_;
                x_curr += v_curr / p_sampling_rate_;
                Eigen::Vector3d current_pos = start_pos + x_curr * direction;

                //check collision
                if (!checkTraversable(current_pos)){
                    new_segment->trajectory.clear();
                    return false;
                }

                // append to result
                mav_msgs::EigenTrajectoryPoint trajectory_point;
                trajectory_point.position_W = current_pos;
                trajectory_point.setFromYaw(defaults::angleScaled(yaw));
                trajectory_point.time_from_start_ns = static_cast<int64_t>(t_curr * 1.0e9);
                new_segment->trajectory.push_back(trajectory_point);
            }
            return true;
        }

    } // namespace trajectory_generators
}  // namespace mav_active_3d_planning