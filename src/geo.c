#include "geo.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double haversine_km(double lat1, double lon1, double lat2, double lon2){
    double dlat=(lat2-lat1)*M_PI/180.0;
    double dlon=(lon2-lon1)*M_PI/180.0;
    double a=sin(dlat/2)*sin(dlat/2)+cos(lat1*M_PI/180.0)*cos(lat2*M_PI/180.0)*sin(dlon/2)*sin(dlon/2);
    double c=2*atan2(sqrt(a),sqrt(1-a));
    return 6371.0*c;
}
bool in_bbox(double lat, double lon, double min_lat, double min_lon, double max_lat, double max_lon){
    return lat>=min_lat && lat<=max_lat && lon>=min_lon && lon<=max_lon;
}
