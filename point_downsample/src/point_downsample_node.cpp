#include <ros/ros.h>
#include <ros/console.h>

#include <std_msgs/Int32.h>
#include <std_msgs/Float64.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/PointCloud2.h>
#include <geometry_msgs/QuaternionStamped.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>

#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>

// PCL specific includessresetd
#include <pcl/ModelCoefficients.h>
#include <pcl-1.6/pcl/ros/conversions.h>
#include <pcl-1.6/pcl/point_cloud.h>
#include <pcl-1.6/pcl/point_types.h>
#include <pcl_ros/transforms.h>
#include <pcl-1.6/pcl/filters/voxel_grid.h>
//#include <pcl-1.7/pcl/filters/plane_clipper3D.h>
#include <pcl-1.6/pcl/features/normal_3d.h>
#include <pcl-1.6/pcl/filters/extract_indices.h>
#include <pcl-1.6/pcl/filters/passthrough.h>

#include <pcl-1.6/pcl/sample_consensus/model_types.h>
#include <pcl-1.6/pcl/sample_consensus/method_types.h>
#include <pcl-1.6/pcl/segmentation/sac_segmentation.h>
#include <pcl-1.6/pcl/segmentation/organized_multi_plane_segmentation.h>
#include <pcl-1.6/pcl/filters/statistical_outlier_removal.h>

#include <pcl-1.6/pcl/features/normal_3d.h>
#include <pcl-1.6/pcl/kdtree/kdtree.h>
#include <pcl-1.6/pcl/octree/octree.h>
//#include <pcl-1.6/pcl
#include <pcl-1.6/pcl/segmentation/extract_clusters.h>

#include "point_downsample/RefreshParams.h"
#include "point_downsample/SetPosition.h"
#include "point_downsample/SetOrientation.h"

#include <QtGui>
#include "olamanager.h"

ros::NodeHandlePtr _nhPtr;

ros::Publisher _pointsPub;
ros::Publisher _backgroundPub;
ros::Publisher _foregroundPub;
ros::Publisher _clustersPub;
ros::Publisher _groundImuPub;
ros::Publisher _visualizerPub;

ros::Subscriber _kinectImuSub;
ros::Subscriber _pointCloudSub;

ros::ServiceServer _refreshParamServ;
ros::ServiceServer _positionmServ;
ros::ServiceServer _orientationServ;

tf::TransformBroadcaster* _tfBroadcaster = NULL;
tf::TransformListener* _tfListener = NULL;

geometry_msgs::Point _kinectPosition;
geometry_msgs::Quaternion _kinectOrientation;

using namespace point_downsample;

/*Function Prototypes*/
//Subscriber callbacks
void imuCallback(const sensor_msgs::Imu::ConstPtr& imuMsg);
void pointCloudCallback (const sensor_msgs::PointCloud2Ptr& input);

//Service callbackes
bool refreshParams(RefreshParams::Request &request, RefreshParams::Response &response);
bool setOrientation(SetOrientation::Request &request, SetOrientation::Response &response);
bool setPosition(SetPosition::Request &request, SetPosition::Response &response);

//Helper functions
void updateTransform();

visualization_msgs::MarkerArrayPtr generateMarkers(float centroid[3], float maxValue[3], float minValue[3], int id);

struct point3d {
    point3d(float values[3]){
        data[0]=values[0];
        data[1]=values[1];
        data[2]=values[2];
    }

    union{
        float data[4];
        struct {
          float x;
          float y;
          float z;
        };
    };
};

struct light_config{
    float shift;
    float spacing;
    float radius;
    int axis;
    DmxAddress origin;
    DmxAddress end;
};

OlaManager* _ola;
light_config _lightConfig;

float getValueByRange(float upper, float lower, float percent, bool reverse=false);
float getDistance(light_config& config, DmxAddress& address, point3d& point);
void updateLights(vector<point3d> centroids);


int main(int argc, char** argv){
    ros::init (argc, argv, "point_downsample");
    _nhPtr = ros::NodeHandlePtr(new ros::NodeHandle());
    _tfListener = new tf::TransformListener();
    tf::TransformBroadcaster _broadcaster;
    _tfBroadcaster = &_broadcaster;


    _kinectImuSub = _nhPtr->subscribe("/imu", 10, imuCallback);
    _pointCloudSub = _nhPtr->subscribe ("/camera/depth/points", 1, pointCloudCallback);

    _pointsPub = _nhPtr->advertise<sensor_msgs::PointCloud2> ("/point_downsample/points", 1);
    _backgroundPub = _nhPtr->advertise<sensor_msgs::PointCloud2> ("/point_downsample/background", 1);
    _clustersPub = _nhPtr->advertise<sensor_msgs::PointCloud2> ("/point_downsample/clusters", 1);
    _foregroundPub = _nhPtr->advertise<sensor_msgs::PointCloud2> ("/point_downsample/foreground", 1);
    _groundImuPub = _nhPtr->advertise<sensor_msgs::Imu> ("/point_downsample/ground_imu", 1);
    _visualizerPub = _nhPtr->advertise<visualization_msgs::MarkerArray>( "/point_downsample/markers", 0 );

    _refreshParamServ = _nhPtr->advertiseService("refresh_params", refreshParams);
    _orientationServ = _nhPtr->advertiseService("set_orientation", setOrientation);
    _positionmServ = _nhPtr->advertiseService("set_position", setPosition);


    //Load settings from parameters
    _kinectPosition.z = 1.5;

    _lightConfig.origin.offset = 0;
    _lightConfig.origin.universe = 1;
    _lightConfig.end.offset = 3*32;
    _lightConfig.end.universe = 1;

    _lightConfig.radius = 0.6;
    _lightConfig.spacing = 0.2286f; //9in in meters
    _lightConfig.shift = 2.9f;      //2.5 meters
    _lightConfig.axis = 2;

    _ola = new OlaManager();

    ros::spin();
}


void imuCallback(const sensor_msgs::Imu::ConstPtr& imuMsg) {
    /*tf::Quaternion orientation;
    tf::quaternionMsgToTF(imuMsg->orientation, orientation);


    tf::Transform transform;
    transform.setOrigin(tf::Vector3(_kinectPosition.x, _kinectPosition.y, _kinectPosition.z));
    transform.setRotation(orientation);

    _tfBroadcaster->sendTransform( tf::StampedTransform(transform, imuMsg->header.stamp, "base_link", "camera_link") );

    */
}

pcl::PointCloud<pcl::PointXYZ>::Ptr backgroundCloud;
sensor_msgs::PointCloud2 backgroundSensor;

void pointCloudCallback (const sensor_msgs::PointCloud2Ptr& input) {
    if(input->data.size() <= 0){
        std::cout << "Input cloud size " << input->data.size() << std::endl;
        return;
    }

    if(_pointsPub.getNumSubscribers() < 1){
        //Short circuit
        //return;
    }

    //sensor_msgs::PointCloud2::Ptr cloud (new sensor_msgs::PointCloud2);
    sensor_msgs::PointCloud2 downSampledInput;


    //Downsample input point cloud
    float leafSize = 0.05f;
    pcl::VoxelGrid<sensor_msgs::PointCloud2> downsample;
    downsample.setInputCloud(input);
    downsample.setLeafSize(leafSize, leafSize, leafSize);
    downsample.filter(downSampledInput);

    pcl::PointCloud<pcl::PointXYZ>::Ptr pclCloud( new pcl::PointCloud<pcl::PointXYZ> );
    pcl::fromROSMsg (downSampledInput, *pclCloud);

    if(backgroundCloud.get() == NULL) {
        std::cout << "Background cloud size " << input->data.size() << ", downsampled size " << downSampledInput.data.size() <<  std::endl;
        backgroundCloud = pclCloud;
        pcl::toROSMsg(*backgroundCloud, backgroundSensor);

        _backgroundPub.publish(backgroundSensor);

        //if(octree.getInputCloud()

        //octree.deleteCurrentBuffer();


        std::cout << "Octree ready" << std::endl;
    }

    pcl::octree::OctreePointCloudChangeDetector<pcl::PointXYZ> octree (0.2f);
    octree.setInputCloud(backgroundCloud);
    octree.addPointsFromInputCloud();

    octree.switchBuffers();

    octree.setInputCloud(pclCloud);
    octree.addPointsFromInputCloud();

    //IndicesConstPtr octIndecess = octree.getIndices();

    std::vector<int> newPointIdxVector;

     // Get vector of point indices from octree voxels which did not exist in previous buffer
    octree.getPointIndicesFromNewVoxels (newPointIdxVector);

    pcl::PointCloud<pcl::PointXYZ> foregroundCloud(*pclCloud, newPointIdxVector);

    float foregroundPerecent = (float)foregroundCloud.points.size() / (float)backgroundCloud->points.size();

    if(foregroundPerecent > 0.15){
        backgroundCloud.reset();
        std::cout << "Reseting foreground percent=" << foregroundPerecent << std::endl;
    }


    //std::cout << "Filtering complete original=" << pclCloud->points.size() << " foreground=" << foregroundCloud.points.size() << std::endl;

    vector<point3d> centroids;

    if(foregroundCloud.points.size() > 0){
        // Creating the KdTree object for the search method of the extraction
        pcl::search::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZ>);
        tree->setInputCloud ( foregroundCloud.makeShared() );

        //std::cout << "Foreground KdTree ready" << std::endl;

        std::vector<pcl::PointIndices> cluster_indices;
        pcl::EuclideanClusterExtraction<pcl::PointXYZ> ec;
        ec.setClusterTolerance (0.12f);
        ec.setMinClusterSize (100);
        ec.setMaxClusterSize (2000);
        ec.setSearchMethod (tree);
        ec.setInputCloud ( foregroundCloud.makeShared() );
        ec.extract (cluster_indices);

        //std::cout << cluster_indices.size() << " clusters" << std::endl;

        int index=0;

        pcl::PointCloud<pcl::PointXYZ> clusterCloud;

        for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin (); it != cluster_indices.end (); ++it){

            float maxValues[3] = {-9000, -9000, -9000};
            float minValues[3] = {9000, 9000, 9000};
            float centroid[3] = {0, 0, 0};

            pcl::PointCloud<pcl::PointXYZ> localCluster(foregroundCloud, it->indices);
            clusterCloud += localCluster;


            for (std::vector<int>::const_iterator pit = it->indices.begin(); pit != it->indices.end(); pit++) {

                for(int i=0; i<3; i++){
                    if(foregroundCloud.points[*pit].data[i] > maxValues[i]){
                        maxValues[i] = foregroundCloud.points[*pit].data[i];
                    }

                    if(foregroundCloud.points[*pit].data[i] < minValues[i]){
                        minValues[i] = foregroundCloud.points[*pit].data[i];
                    }

                    centroid[i] += foregroundCloud.points[*pit].data[i];
                }
            }

            for(int i=0; i<3; i++){
                centroid[i] = centroid[i] / it->indices.size();
            }

            centroids.push_back( (point3d) centroid );

            visualization_msgs::MarkerArrayPtr markers = generateMarkers(centroid, maxValues, minValues, index++);

            _visualizerPub.publish(markers);
        }

        sensor_msgs::PointCloud2 clusterSensor;
        pcl::toROSMsg(clusterCloud, clusterSensor);

        clusterSensor.header.frame_id = input->header.frame_id;

        _clustersPub.publish(clusterSensor);
    }

    updateLights(centroids);

    /*TODO:
     *  -Ground plane detection
     *      -Conditionally transform orientation using Imu
     *      -Compute position & orientation from ground plane
     *      -Update /base_link transform using ground plane
     *
     */

    //Transform into base_link
    /*try{
        pcl_ros::transformPointCloud(std::string("/base_link"), downSampledInput, *cloud, *_tfListener);
    }
    catch(tf::TransformException ex){
        ROS_ERROR("TFException %s",ex.what());
        return;
    }*/

    //cloud->header.frame_id = "/base_link";
    //_pointsPub.publish(cloud);

    //cloud = downSampledInput;

    sensor_msgs::PointCloud2 foregroundSensor;
    pcl::toROSMsg(foregroundCloud, foregroundSensor);

    _foregroundPub.publish(foregroundSensor);
    _pointsPub.publish(downSampledInput);
}


bool refreshParams(RefreshParams::Request &request, RefreshParams::Response &response){
    //TODO

    return false;
}


bool setOrientation(SetOrientation::Request &request, SetOrientation::Response &response){
    _kinectOrientation = request.orientation;

    updateTransform();

    response.success = true;
    return response.success;
}


bool setPosition(SetPosition::Request &request, SetPosition::Response &response){
    //TODO
    _kinectPosition = request.position;

    updateTransform();

    response.success = true;
    return response.success;
}


void updateTransform(){
    /*
     *  If imu_enabled
     *      -set orientation using imu
     *  Else if plane_finder_enabled
     *      -detect largest plane in bottom half of cloud
     *      -compute height above largest plane
     *  Else
     *      -only service calls change positon and orientation
     */
}


visualization_msgs::MarkerArrayPtr generateMarkers(float centroid[3], float maxValue[3], float minValue[3], int id){
    visualization_msgs::Marker centroidMarker;
    centroidMarker.header.frame_id = "/camera_depth_optical_frame";
    centroidMarker.header.stamp = ros::Time();
    centroidMarker.ns = "point_downsample";
    centroidMarker.id = id;
    centroidMarker.type = visualization_msgs::Marker::SPHERE;
    centroidMarker.action = visualization_msgs::Marker::ADD;
    centroidMarker.pose.position.x = centroid[0];
    centroidMarker.pose.position.y = centroid[1];
    centroidMarker.pose.position.z = centroid[2];
    centroidMarker.pose.orientation.x = 0.0;
    centroidMarker.pose.orientation.y = 0.0;
    centroidMarker.pose.orientation.z = 0.0;
    centroidMarker.pose.orientation.w = 1.0;
    centroidMarker.scale.x = 0.3;
    centroidMarker.scale.y = 0.3;
    centroidMarker.scale.z = 0.3;
    centroidMarker.color.a = 0.3;
    centroidMarker.color.r = 1.0;
    centroidMarker.color.g = 1.0;
    centroidMarker.color.b = 0.0;

    float center[3];
    float range[3];

    for(int i=0; i<3; i++){
        range[i] = maxValue[i] - minValue[i];
        center[i] = (range[i] / 2.0f) + minValue[i];

        //cout << "range=" << range[i] << endl;
    }

    visualization_msgs::Marker boundsMarker;
    boundsMarker.header.frame_id = "/camera_depth_optical_frame";
    boundsMarker.header.stamp = ros::Time();
    boundsMarker.ns = "point_downsample";
    boundsMarker.id = id+100;
    boundsMarker.type = visualization_msgs::Marker::CUBE;
    boundsMarker.action = visualization_msgs::Marker::ADD;
    boundsMarker.pose.position.x = center[0];
    boundsMarker.pose.position.y = center[1];
    boundsMarker.pose.position.z = center[2];
    boundsMarker.pose.orientation.x = 0.0;
    boundsMarker.pose.orientation.y = 0.0;
    boundsMarker.pose.orientation.z = 0.0;
    boundsMarker.pose.orientation.w = 1.0;
    boundsMarker.scale.x = range[0];
    boundsMarker.scale.y = range[1];
    boundsMarker.scale.z = range[2];
    boundsMarker.color.a = 0.2;
    boundsMarker.color.r = 0.0;
    boundsMarker.color.g = 0.0;
    boundsMarker.color.b = 1.0;


    visualization_msgs::MarkerArrayPtr markerArray( new visualization_msgs::MarkerArray );


    markerArray->markers.push_back(centroidMarker);
    markerArray->markers.push_back(boundsMarker);

    return markerArray;
}


float getDistance(light_config& config, DmxAddress& address, point3d& point){
    int dmxDelta = (address.getGlobalOffset() - config.origin.getGlobalOffset()) / 3.0f;

    float lightPos = (((float)dmxDelta) * config.spacing) + config.shift;

    return point.data[ config.axis ] - lightPos;
}


enum EffectStates { IDLE, EXPANDING, FADING };
int effectState = IDLE;
double effectPos=0.0f;
double effectRadius = 0.0f;
double maxEffectRadius = 6.0f;
double effectDurationMs = 1000.0f;
double effectExpandCutoffPercent = 0.4f;
double effectPercent = 0.0f;
double effectStatePercent = 0.0f;   //! Percent complete of current effectState
//double effectLowerBound;
//double effectUpperBound;
ros::Time effectStartTime;
ros::Duration effectDeltaTime;

void updateLights(vector<point3d> centroids){
    //float radiusSq = _lightConfig.radius * _lightConfig.radius;
    DmxAddress currentAddress = _lightConfig.origin;

    _ola->blackout();
    ros::Time now = ros::Time::now();

    if(effectState != IDLE){
        //Compute time delta, effect perect, and effect radius
        effectDeltaTime = now - effectStartTime;
        double deltaMs = ((double) effectDeltaTime.toNSec()) / 1000000.0f;

        effectPercent = deltaMs / effectDurationMs;

        if(effectPercent > 1.0f){
            effectState = IDLE;
            effectRadius = 0.0f;
            effectPercent = 0.0f;
            effectStatePercent = 0.0f;
            std::cout << "Effect Idle at deltaMs=" << deltaMs << std::endl;
        }
        else{

            if(effectPercent > effectExpandCutoffPercent){
                //Fade out
                effectState = FADING;
                effectStatePercent = (effectPercent - effectExpandCutoffPercent) / (1.0f - effectExpandCutoffPercent);
                effectRadius = maxEffectRadius;
            }
            else{
                effectState = EXPANDING;
                effectStatePercent = effectPercent / effectExpandCutoffPercent;
                effectRadius = effectStatePercent * maxEffectRadius;
            }

            //effectLowerBound =  effectPos - effectRadius;
            //effectUpperBound =  effectPos + effectRadius;
        }
    }

    while(currentAddress.offset < _lightConfig.end.offset){ //For each light

        int nearestBlobIdx = 0;
        float nearestHeight = 0.0f;
        float minDistance[3] = {-1.0f, -1.0f, -1.0f};

        int nearestCentroidIdx = 0;

        float mostNeg = 0.0f;
        float mostPos = 0.0f;

        for(int i=0; i<centroids.size(); i++){  //For each blob
            float range = getDistance(_lightConfig, currentAddress, centroids[i]);

            if(range < 0.0f && (range > mostNeg || mostNeg == 0.0f)){
                mostNeg = range;
            }
            if(range > 0.0f && (range < mostPos || mostPos == 0.0f)){
                mostPos = range;
            }

            range = fabs(range);

            if(range < minDistance[_lightConfig.axis] || minDistance[_lightConfig.axis]==-1.0f) {
                minDistance[_lightConfig.axis] = range;
                nearestHeight = fabsf(centroids[i].data[1] + 0.8f);
                nearestCentroidIdx = i;
            }
        }

        if(minDistance[_lightConfig.axis] > -1.0f){
            float radiusPercent = fmax(minDistance[_lightConfig.axis] / _lightConfig.radius, 0.01f);


            float value = fmin(1.0f, (powf(5.0f, radiusPercent)-1.0f) / 4.0f);

            float saturation = (nearestHeight / 1.3f);  //Scale saturation with height, shorting things have highest saturation
            float hue = value;


            if(nearestHeight < 0.3f && effectState == IDLE){
                effectState = EXPANDING;
                effectPercent = 0.0f;
                effectPos = centroids[nearestCentroidIdx].data[_lightConfig.axis];
                effectStartTime = now;

                std::cout << "Effect triggered, height="<< nearestHeight << " at pos=" << effectPos << std::endl;
            }

            if(mostNeg != 0.0f && mostPos != 0.0f){
                uint64_t ms = now.toNSec() / 1000000;
                ms = ms % 3000;

                float width = mostPos + fabs(mostNeg);
                float position = (mostPos + mostNeg) / width;

                if(width < (_lightConfig.radius/0.5f)) {
                    hue = hue * 0.7f;
                    hue += 0.3f * fabsf( sinf(  ((((float)ms) / 3000.0f) *2* M_PI) + position ) ) ;
                    saturation = 1.0f;
                }
            }


            if(effectState == EXPANDING){
                //Snake out from pos
                float zero[3] = {0.0f, 0.0f, 0.0f};
                point3d pt(zero);
                float lightPos = fabsf( getDistance(_lightConfig, currentAddress, pt) );

                float lightPosEffectPosDelta = fabs(effectPos - lightPos);

                if(lightPosEffectPosDelta < effectRadius){
                    saturation = 0.0f;
                    value = 0.0f;

                    if(lightPosEffectPosDelta > (effectRadius - 0.3f)){
                        hue = ((float)rand()/(float)RAND_MAX);
                        saturation = 1.0f;
                    }
                }
            }
            else if(effectState == FADING){
                float zero[3] = {0.0f, 0.0f, 0.0f};
                point3d pt(zero);
                float lightPos = fabsf( getDistance(_lightConfig, currentAddress, pt) );

                float lightPosEffectPosDelta = fabs(effectPos - lightPos);

                //Is this light in the effect radius?
                if(lightPosEffectPosDelta < effectRadius){
                    //Scale between 0.0f and saturatation
                    saturation = getValueByRange(saturation, 0.0f, effectStatePercent);
                    value = getValueByRange(value, 0.0f, effectStatePercent);
                }
            }

            QColor color = QColor::fromHsvF(hue, saturation, 1.0f-value);

            _ola->setPixel(currentAddress, color, 0.3f);
        }


        currentAddress.offset += 3;
    }

    _ola->sendBuffers();
}

float getValueByRange(float upper, float lower, float percent, bool reverse){
    float value = (upper - lower) * percent;

    if(reverse){
        value = upper - value;
    }
    else{
        value = lower + value;
    }

    return value;
}
