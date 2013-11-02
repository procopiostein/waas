#include <ros/ros.h>
#include <ros/console.h>

#include <std_msgs/Int32.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/PointCloud2.h>
#include <geometry_msgs/QuaternionStamped.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include <visualization_msgs/InteractiveMarker.h>

#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>

#include <ola/DmxBuffer.h>
#include <ola/OlaClient.h>
#include <ola/StreamingClient.h>

#include <string>
#include <iostream>
#include <sstream>

#include <QtGui>
#include <QtCore>

//#include "point_downsample/RefreshParams.h"
#include "utils.h"
#include "olamanager.h"
#include "pixelmapper.h"

#include "ola_dmx_driver/RefreshParams.h"
#include "starfield.h"

using namespace std;
using namespace ola_dmx_driver;

#define DEFAULT_GLOBE_HEIGHT (3.0f)

ros::NodeHandlePtr _nhPtr;


//Animation_host Publishers
ros::Publisher _blobImagePub;
ros::Publisher _framePub;


//Animation host Subscribers
ros::Subscriber _blobSub;

ros::Subscriber _frameSub;

ros::ServiceServer _refreshParamServ;

ros::Publisher _lightVizPub;
tf::TransformListener* _tfListener = NULL;
tf::TransformBroadcaster* _tfBroadcaster = NULL;

//Service callbackes
bool refreshParams(RefreshParams::Request &request, RefreshParams::Response &response);

double loadRosParam(std::string param, double value=0.0f);
void reloadParameters();

void renderImage(const ros::TimerEvent& event);
void publishGlobeTransform(const ros::TimerEvent& event);
void publishGlobeMarkers();

//Members
OlaManager* _olaManager;
PixelMapper* _pixelMapper;


geometry_msgs::Point _globesScale;
tf::Vector3 _globesOrigin;
tf::Quaternion _globesOrientation;

geometry_msgs::Point _globeSpacing;

QImage* _animationHostImage;

QColor background;
qint64 frameCount;

struct BlobInfo {
    tf::Vector3 centroid;
    tf::Vector3 bounds;
    ros::Time timestamp;
};

class AnimationHost {
    private:
        OlaManager* _olaManager;
        PixelMapper* _pixelMapper;
};

ros::Time lastInteractiveFrame;
QMap<int, BlobInfo> trackedBlobs;

//Callbacks
void frameCallback(const sensor_msgs::ImagePtr& frame);
void blobCallback(const visualization_msgs::MarkerArrayPtr& markers);

int main(int argc, char** argv){
    _olaManager = new OlaManager();
    _olaManager->blackout();

    ros::init (argc, argv, "pixel_map_node");
    _nhPtr = ros::NodeHandlePtr(new ros::NodeHandle());

    _pixelMapper = new PixelMapper(_olaManager);
    lastInteractiveFrame = ros::Time::now();

    if(!_pixelMapper->fromFile()){
        ROS_ERROR("Failed to load pixel map, exiting!");
        return -1;
    }

    background.setHsvF(0.0f, 0.9, 0.5);
    frameCount = 0;


    _tfListener = new tf::TransformListener();
    tf::TransformBroadcaster _broadcaster;
    _tfBroadcaster = &_broadcaster;


    //Setup animation image
    _animationHostImage = new QImage(_pixelMapper->width(), _pixelMapper->height(), QImage::Format_RGB32);
    QPainter painter;
    QRect bounds(0,0, 34, 34);
    QBrush fillBrush( QColor(0,0,0) );
    painter.begin(_animationHostImage);
    painter.fillRect(bounds, fillBrush);
    painter.end();

    //Load parameters
    reloadParameters();

    publishGlobeTransform(ros::TimerEvent());


    _lightVizPub = _nhPtr->advertise<visualization_msgs::MarkerArray> ("/pixel_map_node/globes/markers", 1);
    _blobImagePub = _nhPtr->advertise<sensor_msgs::Image> ("/pixel_map_node/animation/blob_image", 1);
    _framePub = _nhPtr->advertise<sensor_msgs::Image> ("/pixel_map_node/animation/image", 1);

    _frameSub = _nhPtr->subscribe ("/pixel_map_node/animation/blob_image", 1, frameCallback);
    _blobSub = _nhPtr->subscribe("/point_downsample/markers", 1, blobCallback);



    //Services
    _refreshParamServ = _nhPtr->advertiseService("/pixel_map_node/refresh_params", refreshParams);


    publishGlobeMarkers();


    ros::Timer transformTimer = _nhPtr->createTimer(ros::Duration(0.2), publishGlobeTransform);
    ros::Timer renderTimer = _nhPtr->createTimer(ros::Duration(0.033), renderImage);    //30 FPS

    ros::spin();

    delete _pixelMapper;
    delete _olaManager;

	return 0;
}

struct Blip {
    QPointF position;
    QDateTime startTime;
    QDateTime endTime;
    QColor startColor;
    QColor endColor;
};

QVector<Blip> idleBlips;

void updateIdleAnimation(){
    QDateTime now = QDateTime::currentDateTimeUtc();

    /*

    if(idleBlips.count() < 5){
        int newBlips = qrand() % 50;

        int width = _pixelMapper->width();
        int height = _pixelMapper->height();

        for(int i=0; i<newBlips; i++){
            int delayMs = qrand() % 25000;
            int durationMs = qrand() % 15000;

            Blip b;

            b.position.setX( qrand() % width );
            b.position.setY( qrand() % height );
            b.startTime = now.addMSecs(delayMs);
            b.endTime = b.startTime.addMSecs(delayMs);

            b.startColor = QColor(Qt::black);
            b.endColor = QColor(Qt::white);

            idleBlips.push_back(b);
        }
    }

    for(int i=0; i<idleBlips.size(); i++){
        Blip b = idleBlips[i];
        
        if(b.endTime < now){
            
        }
        
        qreal progress = 
    }

    */
}



void renderImage(const ros::TimerEvent& event){
    ros::Duration idleDuration = ros::Time::now() - lastInteractiveFrame;

    qreal hue = (1.0f / 300.0f) * (qreal)(frameCount % 300);

    background.setHsvF( hue, background.saturationF(), background.valueF() );

    _pixelMapper->setBackgroundColor(background);

    if(idleDuration.toSec() > 30){
        //Update idle animation
    }


    if(_pixelMapper->isDirty()) {

        _pixelMapper->render();
    }


    QImage* img = _pixelMapper->getImage();

    sensor_msgs::Image frame;

    frame.width = img->width();
    frame.height = img->height();

    frame.header.frame_id = "base_link";
    frame.header.stamp = ros::Time();

    frame.encoding = sensor_msgs::image_encodings::RGB8;

    frame.data.clear();
    frame.step = img->width() * 3;

    for(int j=0; j<img->height(); j++){
        for(int i=img->width()-1; i >= 0; i--){

            QRgb pixel = img->pixel(i, j);

            frame.data.push_back( qRed(pixel) );
            frame.data.push_back( qGreen(pixel) );
            frame.data.push_back( qBlue(pixel) );

        }
    }

    _framePub.publish( frame );

    frameCount++;
}

void publishGlobeTransform(const ros::TimerEvent& event){
    tf::Transform transform;

    transform.setOrigin( _globesOrigin );
    transform.setRotation( _globesOrientation );
    _tfBroadcaster->sendTransform(tf::StampedTransform(transform, ros::Time::now(), "base_link", "globes_link"));

    if(_lightVizPub.getNumSubscribers() > 0){
        publishGlobeMarkers();;
    }
}

void publishGlobeMarkers(){
    QMap<int, QPair<QPoint, QRgb> > pixelData = _pixelMapper->getGlobeData();

    visualization_msgs::MarkerArrayPtr markerArray(new visualization_msgs::MarkerArray);

    QMap<int, QPair<QPoint, QRgb> >::iterator pixelIter = pixelData.begin();

    int id=0;
    for(pixelIter; pixelIter != pixelData.end(); pixelIter++){
        visualization_msgs::Marker globeMarker;
        globeMarker.header.frame_id = "/globes_link";
        globeMarker.ns = "pixel_map_node";
        globeMarker.id = id;
        globeMarker.type = visualization_msgs::Marker::CUBE;
        globeMarker.action = visualization_msgs::Marker::ADD;
        globeMarker.pose.position.x = pixelIter.value().first.x() * _globeSpacing.x;
        globeMarker.pose.position.y = pixelIter.value().first.y() * _globeSpacing.y;
        globeMarker.pose.position.z = 0;
        globeMarker.pose.orientation.x = 0.0;
        globeMarker.pose.orientation.y = 0.0;
        globeMarker.pose.orientation.z = 0.0;
        globeMarker.pose.orientation.w = 1.0;
        globeMarker.scale.x = 0.05;
        globeMarker.scale.y = 0.05;
        globeMarker.scale.z = 0.05;
        globeMarker.color.a = 1.0;
        globeMarker.color.r = qRed(pixelIter.value().second);
        globeMarker.color.g = qGreen(pixelIter.value().second);
        globeMarker.color.b = qBlue(pixelIter.value().second);

        markerArray->markers.push_back(globeMarker);

        id++;
    }

    _lightVizPub.publish(markerArray);
}



void frameCallback(const sensor_msgs::ImagePtr& frame) {
    lastInteractiveFrame = frame->header.stamp;
    _pixelMapper->updateImage(frame);
}


void blobCallback(const visualization_msgs::MarkerArrayPtr& markers) {

    try{
        if(!_tfListener->canTransform("base_link", "globes_link", ros::Time())){
            std::cout<<"blobCallback() - Can't transform"<<std::endl;
            return;
        }
    }
    catch(...){
        std::cout<<"blobCallback() - Caught transform exception"<<std::endl;
        return;
    }

    QPainter painter;
    painter.begin( _animationHostImage );

    _animationHostImage->fill(QColor());

    for(int i=0; i<markers->markers.size(); i++){
        visualization_msgs::Marker& marker = markers->markers.at(i);

        geometry_msgs::PoseStamped poseInput;
        poseInput.header = marker.header;
        poseInput.pose = marker.pose;

        if(marker.type == visualization_msgs::Marker::CUBE){

            geometry_msgs::PoseStamped globeLinkPose;
            _tfListener->transformPose("globes_link", poseInput, globeLinkPose);

            double deltaXPx = (marker.scale.x * _globesScale.x) / 1.75f; //1.75 is aestecic not real conversion
            double deltaYPx = (marker.scale.y * _globesScale.y) / 1.75f;
            double deltaZPx = (marker.scale.z * _globesScale.z) / 1.75f;


            double centerXPx = (globeLinkPose.pose.position.x * _globesScale.x);
            double centerYPx = (globeLinkPose.pose.position.y * _globesScale.y);

            double radiusPx = qMax(deltaXPx, deltaYPx);

            QRectF bounds(centerXPx - (deltaXPx/2.0f),
                          centerYPx - (deltaYPx/2.0f),
                          radiusPx,
                          radiusPx);

            /*BlobInfo blob;
            blob.bounds.setX( widthPx );
            blob.bounds.setY( depthPx );*/

            //cout << "(" << globeLinkPose.pose.position.x << "," << globeLinkPose.pose.position.y << ") -> (" <<centerXPx << "," << centerYPx << ")" <<endl;

            QBrush fillBrush;


            if(radiusPx > 5.5f){
                QConicalGradient conicalGrad(centerXPx,centerYPx, 0);
                conicalGrad.setColorAt(0, Qt::red);
                conicalGrad.setColorAt(90.0/360.0, Qt::green);
                conicalGrad.setColorAt(180.0/360.0, Qt::blue);
                conicalGrad.setColorAt(270.0/360.0, Qt::magenta);
                conicalGrad.setColorAt(360.0/360.0, Qt::yellow);

                fillBrush = QBrush( conicalGrad );

            }
            else {
                QRadialGradient radialGrad(QPointF(centerXPx,centerYPx), radiusPx);
                radialGrad.setColorAt(0, Qt::white);
                radialGrad.setColorAt(1.0f, Qt::black);

                fillBrush = QBrush( radialGrad );
            }


            //QBrush fillBrush( radialGrad );
            QPainterPath fillPath;

            fillPath.addEllipse(bounds);

            painter.fillPath(fillPath, fillBrush);
            //painter.save();

        }
    }

    painter.end();



    sensor_msgs::Image frame;

    frame.width = _animationHostImage->width();
    frame.height = _animationHostImage->height();

    frame.header.frame_id = "base_link";
    frame.header.stamp = ros::Time();

    frame.encoding = sensor_msgs::image_encodings::RGB8;

    frame.data.clear();
    frame.step = _animationHostImage->width() * 3;

    for(int j=0; j<_animationHostImage->height(); j++){
        for(int i=_animationHostImage->width()-1; i >= 0; i--){

            QRgb pixel = _animationHostImage->pixel(i, j);

            frame.data.push_back( qRed(pixel) );
            frame.data.push_back( qGreen(pixel) );
            frame.data.push_back( qBlue(pixel) );

        }
    }

    _blobImagePub.publish( frame );
}


double loadRosParam(std::string param, double value){
    if(_nhPtr->hasParam( param )){
         _nhPtr->getParam( param, value );
    }
    else{
        _nhPtr->setParam( param, value );
    }

    return value;
}

bool refreshParams(RefreshParams::Request &request, RefreshParams::Response &response){
    reloadParameters();

    return true;
}


void reloadParameters(){
    std::cout << "Reloading parameters ... ";

    //Update position
    _globesOrigin.setX( loadRosParam("/waas/globes/position/x", 0.0f) );
    _globesOrigin.setY( loadRosParam("/waas/globes/position/y", 0.0f) );
    _globesOrigin.setZ( loadRosParam("/waas/globes/position/z", DEFAULT_GLOBE_HEIGHT));

    double deg2radCoef = M_PI / 180.0f;

    //Update orientation
    _globesOrientation.setRPY(
                                deg2radCoef * loadRosParam("/waas/globes/orientation/roll"),
                                deg2radCoef * loadRosParam("/waas/globes/orientation/pitch"),
                                deg2radCoef * loadRosParam("/waas/globes/orientation/yaw")
                              );

    _globesScale.x = loadRosParam("/waas/globes/scale", 1.0f/0.2032f);
    _globesScale.y = _globesScale.x;

    _globeSpacing.x = loadRosParam("/waas/globes/spacing/x", 0.2032);    //Default to 8in
    _globeSpacing.y = loadRosParam("/waas/globes/spacing/y", 0.2032);    //Default to 8in

    std::cout << "done!" << std::endl;
}

