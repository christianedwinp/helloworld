#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/nonfree/features2d.hpp>

//image window name
static const std::string OPENCV_WINDOW = "Image window";

using namespace cv;
Mat img_1, img_2, img_3, img_4, img_5;

class ImageConverter
{
  ros::NodeHandle nh_;
  image_transport::ImageTransport it_;
  image_transport::Subscriber image_sub_;
  image_transport::Publisher image_pub_;

public:
  ImageConverter()
    : it_(nh_)
  {
    //Subscribe to VREP image and publish converted image
    image_sub_ = it_.subscribe("/vrep/image", 1, &ImageConverter::imageCb, this);
    image_pub_ = it_.advertise("/rosopencv_interface/image", 1);

    //OpenCV HighGUI calls to create a display window on start-up
    cv::namedWindow(OPENCV_WINDOW);
  }

  //OpenCV HighGUI calls to destroy a display window on shutdown
  ~ImageConverter()
  {
    cv::destroyWindow(OPENCV_WINDOW);
  }

  //subscriber callback
  void imageCb(const sensor_msgs::ImageConstPtr& msg)
  {
    //convert ROS image to CvImage that is suitable to work with OpenCV
    //must use 'try' and 'catch' format
    cv_bridge::CvImagePtr cv_ptr;
    try
    {
      //convert to suitable color encoding scheme
      /*color encoding scheme:
       * mono8  : CV_8UC1, grayscale image
       * mono16 : CV_16UC1, 16-bit grayscale image
       * bgr8   : CV_8UC3, color image with blue-green-red color order
       * rgb8   : CV_8UC3, color image with red-green-blue color order
       * bgra8  : CV_8UC4, BGR color image with an alpha channel
       * rgba8  : CV_8UC4, RGB color image with an alpha channel
      */
      cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
    }
    catch (cv_bridge::Exception& e)
    {
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return;
    }

    // =============================================
    // FLIPPED IMAGE
    // =============================================
    cv::Mat flippedImage;
    /* flip image
    * flipcode = 0 -> flip on X axis
    * flipcode > 0 -> flip on Y axis
    * flipcode < 0 -> flip on both axis
    */
    cv::flip(cv_ptr->image, flippedImage, 1);

    // =============================================
    // SURF FLANNMATCHER + HOMOGRAPHY
    // =============================================
    //-- Step 1: Detect the keypoints using SURF Detector
    int minHessian = 400;
    SurfFeatureDetector detector( minHessian );
    std::vector<KeyPoint> keypoints_1, keypoints_2, keypoints_3, keypoints_4, keypoints_5, keypoints_stream;
    detector.detect( img_1, keypoints_1 );
    // detector.detect( img_2, keypoints_2 );
    // detector.detect( img_3, keypoints_3 );
    // detector.detect( img_4, keypoints_4 );
    // detector.detect( img_5, keypoints_5 );
    detector.detect( flippedImage, keypoints_stream );

    //-- Step 2: Calculate descriptors (feature vectors)
    // cv::Ptr<cv::DescriptorExtractor> extractor = new cv::SurfDescriptorExtractor;
    SurfDescriptorExtractor extractor;
    Mat descriptors_1, descriptors_2, descriptors_3, descriptors_4, descriptors_5, descriptors_stream;
    extractor.compute( img_1, keypoints_1, descriptors_1 );
    // extractor.compute( img_2, keypoints_2, descriptors_2 );
    // extractor.compute( img_3, keypoints_3, descriptors_3 );
    // extractor.compute( img_4, keypoints_4, descriptors_4 );
    // extractor.compute( img_5, keypoints_5, descriptors_5 );
    extractor.compute( flippedImage, keypoints_stream, descriptors_stream );

    // //-- Step 3: Matching descriptor vectors using FLANN matcher 
    FlannBasedMatcher matcher;
    //matching process pic001.jpg
    std::vector< DMatch > matches;
    matcher.match( descriptors_1, descriptors_stream, matches );

    // //-- Quick calculation of max and min distances between keypoints_1
    double max_dist = 0; double min_dist = 100;
    for( int i = 0; i < descriptors_1.rows; i++ ){ 
      double dist = matches[i].distance;
      if( dist < min_dist ) min_dist = dist;
      if( dist > max_dist ) max_dist = dist;
    }
    printf("-- Max dist : %f \n", max_dist );
    printf("-- Min dist : %f \n", min_dist );

    // //-- Get good matches pic001.jpg
    std::vector< DMatch > good_matches;
    for( int i = 0; i < descriptors_1.rows; i++ ){ 
      if( matches[i].distance < 3*min_dist ){ 
        good_matches.push_back( matches[i]);
      }
    }
    
    Mat img_matches;
    drawMatches( img_1, keypoints_1, flippedImage, keypoints_stream,
               good_matches, img_matches, Scalar::all(-1), Scalar::all(-1),
               vector<char>(), DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS );


    // //-- Localize pic001.jpg
    std::vector<Point2f> obj1;
    std::vector<Point2f> stream;
    for( int i = 0; i < good_matches.size(); i++ ){
        //-- Get the keypoints from the good matches
        obj1.push_back( keypoints_1[ good_matches[i].queryIdx ].pt );
        stream.push_back( keypoints_stream[ good_matches[i].trainIdx ].pt );
    }
    Mat H = findHomography( obj1, stream, CV_RANSAC );

    // //-- Get the corners from the pic001.jpg
    std::vector<Point2f> obj1_corners(4);
    obj1_corners[0] = cvPoint(0,0); 
    obj1_corners[1] = cvPoint( img_1.cols, 0 );
    obj1_corners[2] = cvPoint( img_1.cols, img_1.rows ); 
    obj1_corners[3] = cvPoint( 0, img_1.rows );
    std::vector<Point2f> stream_corners(4);

    perspectiveTransform( obj1_corners, stream_corners, H);

    // //-- Draw pic001.jpg border line in camera stream
    line( img_matches, stream_corners[0] + Point2f( img_1.cols, 0), stream_corners[1] + Point2f( img_1.cols, 0), Scalar(0, 255, 0), 4 );
    line( img_matches, stream_corners[1] + Point2f( img_1.cols, 0), stream_corners[2] + Point2f( img_1.cols, 0), Scalar( 0, 255, 0), 4 );
    line( img_matches, stream_corners[2] + Point2f( img_1.cols, 0), stream_corners[3] + Point2f( img_1.cols, 0), Scalar( 0, 255, 0), 4 );
    line( img_matches, stream_corners[3] + Point2f( img_1.cols, 0), stream_corners[0] + Point2f( img_1.cols, 0), Scalar( 0, 255, 0), 4 );

    // Update GUI Window
    cv::imshow(OPENCV_WINDOW, img_matches);
    cv::waitKey(3);

    // Output modified image stream
    cv_bridge::CvImage img_bridge;
    sensor_msgs::Image img_msg;
    std_msgs::Header header;
    img_bridge = cv_bridge::CvImage(header, sensor_msgs::image_encodings::BGR8, flippedImage); //convert CVimage to ROS
    img_bridge.toImageMsg(img_msg); 
    image_pub_.publish(img_msg); 
  }
};

int main(int argc, char** argv)
{
  ros::init(argc, argv, "rosopencv_interface");
  
  if( argc != 2)
    {
     std::cout << "Missing arguments" << std::endl;
     return -1;
    }
    
  // img_1 = imread( argv[1], CV_LOAD_IMAGE_COLOR ); //load pic001.jpg
  // img_2 = imread( argv[2], CV_LOAD_IMAGE_COLOR ); //load pic002.jpg
  // img_3 = imread( argv[3], CV_LOAD_IMAGE_COLOR ); //load pic003.jpg
  // img_4 = imread( argv[4], CV_LOAD_IMAGE_COLOR ); //load pic004.jpg
  // img_5 = imread( argv[5], CV_LOAD_IMAGE_COLOR ); //load pic005.jpg

  // if( !img_1.data || !img_2.data || !img_3.data || !img_4.data || !img_5.data )
  //     { std::cout<< " --(!) Error reading images " << std::endl; return -1; }

    img_1 = imread( argv[1], CV_LOAD_IMAGE_COLOR ); //load pic001.jpg

  if( !img_1.data )
      { std::cout<< " --(!) Error reading images " << std::endl; return -1; }

  ImageConverter ic;
  ros::spin();
  return 0;
}