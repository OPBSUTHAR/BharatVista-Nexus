#pragma once
#include <stdbool.h>

double haversine_km(double lat1, double lon1, double lat2, double lon2);
bool in_bbox(double lat, double lon, double min_lat, double min_lon, double max_lat, double max_lon);
