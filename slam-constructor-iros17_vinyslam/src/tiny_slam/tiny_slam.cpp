#include <iostream>
#include <ros/ros.h>
#include <sensor_msgs/LaserScan.h>
#include <memory>

#include <nav_msgs/OccupancyGrid.h>

#include "../ros/topic_with_transform.h"
#include "../ros/laser_scan_observer.h"
#include "../ros/init_utils.h"

#include "../core/sensor_data.h"
#include "../core/maps/grid_cell_strategy.h"

#include "tiny_world.h"
#include "tiny_grid_cell.h"

std::shared_ptr<GridCell> init_cell_prototype(TinyWorldParams &params) {
  std::string cell_type;
  ros::param::param<std::string>("~slam/cell/type", cell_type, "avg");

  if (cell_type == "base") {
    params.localized_scan_quality = 0.2;
    params.raw_scan_quality = 0.1;
    return std::make_shared<BaseTinyCell>();
  } else if (cell_type == "avg") {
    params.localized_scan_quality = 0.9;
    params.raw_scan_quality = 0.6;
    return std::make_shared<AvgTinyCell>();
  } else {
    std::cerr << "Unknown cell type: " << cell_type << std::endl;
    std::exit(-1);
  }
}

TinyWorldParams init_common_world_params() {
  double sig_XY, sig_T, width;
  int lim_bad, lim_totl;
  ros::param::param<double>("~slam/scmtch/MC/sigma_XY", sig_XY, 0.2);
  ros::param::param<double>("~slam/scmtch/MC/sigma_theta", sig_T, 0.1);
  ros::param::param<int>("~slam/scmtch/MC/limit_of_bad_attempts", lim_bad, 20);
  ros::param::param<int>("~slam/scmtch/MC/limit_of_total_attempts",
                         lim_totl, 100);
  ros::param::param<double>("~tinySLAM/hole_width", width, 0.5);

  return TinyWorldParams({sig_XY, sig_T, unsigned(lim_bad), unsigned(lim_totl)},
                         width);
}

using ObservT = sensor_msgs::LaserScan;
using TinySlamMap = TinyWorld::MapType;

int main(int argc, char** argv) {
  ros::init(argc, argv, "tinySLAM");

  // init tiny slam
  TinyWorldParams params = init_common_world_params();
  GridMapParams map_params = init_grid_map_params();
  auto cost_est = std::make_shared<TinyScanCostEstimator>();
  auto gcs = std::make_shared<GridCellStrategy>(
    init_cell_prototype(params), cost_est, init_occ_estimator());
  auto slam = std::make_shared<TinyWorld>(gcs, params, map_params);

  // connect the slam to a ros-topic based data provider
  ros::NodeHandle nh;
  double ros_map_publishing_rate, ros_tf_buffer_size;
  int ros_filter_queue, ros_subscr_queue;
  init_constants_for_ros(ros_tf_buffer_size, ros_map_publishing_rate,
                         ros_filter_queue, ros_subscr_queue);
  auto scan_provider = std::make_unique<TopicWithTransform<ObservT>>(
    nh, "laser_scan", tf_odom_frame_id(), ros_tf_buffer_size,
    ros_filter_queue, ros_subscr_queue
  );
  auto scan_obs = std::make_shared<LaserScanObserver>(
    slam, init_skip_exceeding_lsr());
  scan_provider->subscribe(scan_obs);

  auto occup_grid_pub_pin = create_occupancy_grid_publisher<TinySlamMap>(
    slam.get(), nh, ros_map_publishing_rate);

  auto pose_pub_pin = create_pose_correction_tf_publisher<ObservT, TinySlamMap>(
    slam.get(), scan_provider.get());

  ros::spin();
}