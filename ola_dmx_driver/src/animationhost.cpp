#include "animationhost.h"


BlobTracker::BlobTracker(QSharedPointer<RenderData> data) {
    _dataPtr = data;
    _maxAgeMs = 5000;
    _maxJoinRadius = 0.8;
}

void BlobTracker::setMaxAgeMs(quint64 ms) {
    _maxAgeMs = ms;
}

void BlobTracker::setMaxJoinRadius(qreal radius) {
    _maxJoinRadius = radius;
}

void BlobTracker::insertBlob(BlobInfo* b){
    int closestGroup = -1;
    tfScalar closestDistance = 0.0f;

    QMultiMap<int,BlobInfo*>::iterator currentIter = _dataPtr->blobs.begin();

    for(; currentIter != _dataPtr->blobs.end(); currentIter++){
        tfScalar distance = currentIter.value()->centroid.distance( b->centroid );

        if(closestGroup == -1 || closestDistance > distance) {
            closestDistance = distance;
            closestGroup = currentIter.key();
        }
    }

    if(closestGroup != -1){
        if(closestDistance > _maxJoinRadius) {
            closestGroup = -1;
        }
    }

    if(closestGroup == -1){
        if(_dataPtr->blobs.count() > 0) {
            closestGroup = _dataPtr->blobs.keys().last() + 1;
        }
        else {
            closestGroup = 0;
        }
    }

    _dataPtr->blobs.insert(closestGroup, b);
}

void BlobTracker::updateBlobs(QList<BlobInfo*> blobs) {
    if(_dataPtr->blobs.count() > 1){
        std::cout << "Updating " << _dataPtr->blobs.count() << " blobs in "
                  << _dataPtr->blobs.keys().count() << " clusters" <<  std::endl;
    }

    QMultiMap<int,BlobInfo*>::iterator currentIter = _dataPtr->blobs.begin();
    while(currentIter != _dataPtr->blobs.end()){
        ros::Duration age = _dataPtr->timestamp - currentIter.value()->timestamp;
        if( (age.toSec() * 1000) > _maxAgeMs ) {

            std::cout << "Deleting blob, too old age=" << age.toSec() << " sec" << std::endl;

            delete currentIter.value();
            currentIter = _dataPtr->blobs.erase(currentIter);
        }
        else{
            currentIter++;
        }
    }

    if(blobs.size() > 0){
        std::cout << "Got " << blobs.size() << " new blobs" << std::endl;
    }

    for(int i=0; i < blobs.count(); i++){
        insertBlob(blobs[i]);
    }
}


AnimationHost::AnimationHost(QString pixelMapPath, QSharedPointer<RenderData> data) {
    _dataPtr = data;

    _olaManager = new OlaManager();
    _olaManager->blackout();

    _pixelMapper = new PixelMapper(_olaManager);

    if(!_pixelMapper->fromFile(pixelMapPath)){
        ROS_ERROR("Failed to load pixel map, exiting!");
        exit(-2);
    }
}

AnimationHost::~AnimationHost() {
    delete _olaManager;
    delete _pixelMapper;
}

PixelMapper* AnimationHost::getPixelMapper() const {
    return _pixelMapper;
}

Animation* AnimationHost::insertLayer(int layer, Animation* a) {
    Animation* current = getLayer(layer);

    if(current != NULL){
        removeLayer(layer);
    }

    _animations.insert(layer, a);

    return current;
}


void AnimationHost::removeLayer(int layer) {
    _animations.remove(layer);
}


Animation* AnimationHost::getLayer(int layer) {
    return _animations.value(layer, NULL);
}


void AnimationHost::transmit() {
    _pixelMapper->render();
}

QImage* AnimationHost::renderAll() {
    if(_animations.count() > 0){

        return renderLayer( _animations.keys().first(), _animations.keys().last() );
    }

    return NULL;
}


QImage* AnimationHost::renderLayer(int minLayer, int maxLayer) {
    if(maxLayer < minLayer) {
        maxLayer = minLayer;
    }

    QImage* frame = _pixelMapper->getImage();
    RenderData* data = _dataPtr.data();
    QMap<int,Animation*>::iterator layerIter = _animations.begin();

    for(; layerIter != _animations.end(); layerIter++) {

        int layer = layerIter.key();
        if(layer < minLayer || layer > maxLayer) { continue; }

        layerIter.value()->renderFrame(frame, *data);
    }

    return frame;
}

