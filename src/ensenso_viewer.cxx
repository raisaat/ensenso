/*  
*   MIT License
*   
*   Copyright (c) December 2016 
*
*   Permission is hereby granted, free of charge, to any person obtaining a copy
*   of this software and associated documentation files (the "Software"), to deal
*   in the Software without restriction, including without limitation the rights
*   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*   copies of the Software, and to permit persons to whom the Software is
*   furnished to do so, subject to the following conditions:
*   
*   The above copyright notice and this permission notice shall be included in all
*   copies or substantial portions of the Software.
*   
*   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*   SOFTWARE.
*
* 
*  Author: Olalekan P. Ogunmolu
*/
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <algorithm>

#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/image_encodings.h>

#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <image_transport/image_transport.h>
#include <pcl_conversions/pcl_conversions.h>

/*pcl and cv headers*/
#include <ensenso/ensenso_headers.h>
#include <ensenso/visualizer.h>

/*typedefs*/
using PairOfImages =  std::pair<pcl::PCLImage, pcl::PCLImage>;  //for the Ensenso grabber callback 
using pcl_viz = pcl::visualization::PCLVisualizer;
using namespace pathfinder;

#define OUT(__x__) std::cout << __x__ << std::endl;

/*pointers*/
pcl::EnsensoGrabber::Ptr ensenso_ptr;

/*Globals*/
bool running;
bool save = false;
bool updateCloud = false;
size_t counter = 0;
std::ostringstream oss;
const std::string cloudName = "ensenso cloud";
sensor_msgs::PointCloud2 pcl2_cloud;   //msg to be displayed in rviz
boost::shared_ptr<visualizer> viz(new visualizer);
auto paths = pathfinder::getCurrentPath();
auto pwd = std::get<0>(paths);
auto train_imgs = std::get<3>(paths);
auto train_clouds = std::get<4>(paths);
auto test_imgs = std::get<5>(paths);
auto test_clouds = std::get<6>(paths);
ROS_INFO_STREAM("train_imgs: "<< train_imgs << "\ttrain_clouds: " << train_clouds );
boost::shared_ptr<pcl_viz> viewer = viz->createViewer();
std::vector<int> compression_params{cv::IMWRITE_PNG_COMPRESSION, 5};
auto writer = viz->getPCDWriter();

void quit()
{
  ROS_INFO("called quit");
  cv::destroyAllWindows();

  ros::shutdown();
}

void saveCloudAndImage(const boost::shared_ptr<PointCloudT>& cloud, const cv::Mat& image)
{
  oss.str("");
  oss << "./" << std::setfill('0') << std::setw(4) << counter;
  const std::string baseName = oss.str();
  const std::string cloud_id = "train_" + baseName + "_cloud.pcd";
  const std::string imageName = "train_" + baseName + "_image.png";

  ROS_INFO_STREAM("saving cloud: " << cloudName);
  writer.writeBinary(cloud_id, *cloud);
  ROS_INFO_STREAM("saving image: " << imageName);
  cv::imwrite(imageName, image, compression_params);

  ROS_INFO_STREAM("saving complete!");
  ++counter;
}

void grabberCallback (const boost::shared_ptr<PointCloudT>& cloud, const boost::shared_ptr<PairOfImages>& images)
{
  ros::NodeHandle nh;
  image_transport::ImageTransport it(nh);
  image_transport::Publisher imagePub = it.advertise("/camera/image", 1);
  ros::Publisher pclPub = nh.advertise<sensor_msgs::PointCloud2>("/camera/rospy_cloud", 1);

  pcl::visualization::PointCloudColorHandlerCustom<PointT> color_handler (cloud, 255, 255, 255);
  /*populate the cloud viewer and prepare for publishing*/
  if(updateCloud){    
    viewer->removePointCloud(cloudName);
  }
  viewer->addPointCloud(cloud, color_handler, cloudName);
  viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE,\
                     3, cloudName);

  //prepare cloud for rospy publishing
  pcl::toROSMsg(*cloud, pcl2_cloud);
  pcl2_cloud.header.stamp = ros::Time::now();
  pcl2_cloud.header.frame_id = "rospy_cloud";

  /*Process Image and prepare for publishing*/
  unsigned char *l_image_array = reinterpret_cast<unsigned char *> (&images->first.data[0]);
  unsigned char *r_image_array = reinterpret_cast<unsigned char *> (&images->second.data[0]);

  // std::cout << "Encoding: " << images->first.encoding << std::endl;
  
  int type = getOpenCVType (images->first.encoding);
  cv::Mat l_image (images->first.height, images->first.width, type, l_image_array);
  cv::Mat r_image (images->first.height, images->first.width, type, r_image_array);
  cv::Mat im (images->first.height, images->first.width * 2, type);

  im.adjustROI (0, 0, 0, -0.5*images->first.width);
  l_image.copyTo (im);
  im.adjustROI (0, 0, -0.5*images->first.width, 0.5*images->first.width);
  r_image.copyTo (im);
  im.adjustROI (0, 0, 0.5*images->first.width, 0);
  //prepare image and pcl to be published for rospy
  sensor_msgs::ImagePtr msg = cv_bridge::CvImage(std_msgs::Header(), images->first.encoding, im).toImageMsg();

  /*Display cloud and image*/
  cv::imshow("Ensenso images", im);
  int key = cv::waitKey(1);
  switch(key & 0xFF)
  {
    case 27:
    case 'q':
      quit();
    case 's':
      saveCloudAndImage(cloud, im);
  }

  /*Publish the damn image and cloud*/
  ros::Rate rate(5);
  if(ros::ok()){
    imagePub.publish(msg);
    pclPub.publish(pcl2_cloud);
    // ros::spinOnce();
    rate.sleep();
  }
}

int main (int argc, char** argv)
{
  ros::init(argc, argv, "ensensor_publisher_node");  

  ros::AsyncSpinner spinner(4);
  spinner.start();
  ROS_INFO("Started node %s", ros::this_node::getName().c_str());
  running = updateCloud = true;

  ensenso_ptr.reset (new pcl::EnsensoGrabber);
  ensenso_ptr->openTcpPort ();
  ensenso_ptr->openDevice ();
  ensenso_ptr->enumDevices ();

  //ensenso_ptr->initExtrinsicCalibration (5); // Disable projector if you want good looking images.
  boost::function<void(const boost::shared_ptr<PointCloudT>&, const boost::shared_ptr<PairOfImages>&)> f \
                                = boost::bind(&grabberCallback, _1, _2);
  ensenso_ptr->registerCallback (f);

  cv::namedWindow("Ensenso images", cv::WINDOW_NORMAL);
  cv::resizeWindow("Ensenso images", 640, 480) ;
  ensenso_ptr->start ();

  std::chrono::milliseconds duration(10);
  while(!viewer->wasStopped())
  {
    viewer->spinOnce(10);
    std::this_thread::sleep_for(duration);
  }

  ensenso_ptr->stop ();
  ensenso_ptr->closeDevice ();
  ensenso_ptr->closeTcpPort ();

  if(!running)
  {   
    quit();
    spinner.stop();
  }

  return EXIT_SUCCESS;
}

