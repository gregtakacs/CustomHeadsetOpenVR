#include "RadialBezierDistortionProfile.h"

typedef RadialBezierDistortionProfile::DistortionPoint DistortionPoint;

// calculates a point on a cubic Bezier curve given a parameter t and a set of control points.
DistortionPoint BezierPoint(float t, const std::vector<DistortionPoint>& controlPoints){
	float tSquared = t * t;
	float oneMinusT = 1 - t;
	float oneMinusTSquared = oneMinusT * oneMinusT;
	
	float pointX = (
		pow(oneMinusT, 3) * controlPoints[0].degree +
		3 * oneMinusTSquared * t * controlPoints[1].degree +
		3 * oneMinusT * tSquared * controlPoints[2].degree +
		pow(t, 3) * controlPoints[3].degree
	);
	float pointY = (
		pow(oneMinusT, 3) * controlPoints[0].position +
		3 * oneMinusTSquared * t * controlPoints[1].position +
		3 * oneMinusT * tSquared * controlPoints[2].position +
		pow(t, 3) * controlPoints[3].position
	);
	
	return DistortionPoint{pointX, pointY};
}

// SmoothPoints takes a list of points and returns a new list of points with additional points inserted between each pair of points using bezier curves.
std::vector<DistortionPoint> SmoothPoints(const std::vector<DistortionPoint>& points, int innerPointCounts){
	// how far out to move the center bezier points from the existing points
	// larger values will make the curve more "smooth" and less "sharp" at the existing points
	float smoothAmount = 1.0f / 3.0f;
	std::vector<DistortionPoint> outPoints;
	for(int i = 0; i < points.size() - 1; i++){
		// the new points will be inserted between existing points
		DistortionPoint prevPoint = points[i];
		DistortionPoint nextPoint = points[i + 1];
		DistortionPoint prevPrevPoint = i <= 0 ? points[i] : points[i - 1];
		DistortionPoint nextNextPoint = i >= points.size() - 2 ? points[i + 1] : points[i + 2];
		// find slope for prev and next point based on the points that surround them
		float fallbackSlope = (nextPoint.position - prevPoint.position) / (nextPoint.degree - prevPoint.degree);
		float prevSlope = i <= 0 ? fallbackSlope : (nextPoint.position - prevPrevPoint.position) / (nextPoint.degree - prevPrevPoint.degree);
		float nextSlope = (i >= points.size() - 2) ? fallbackSlope : (nextNextPoint.position - prevPoint.position) / (nextNextPoint.degree - prevPoint.degree);
		// extrapolate center points based on the slopes
		float centerDistance = (nextPoint.degree - prevPoint.degree) * smoothAmount;
		float centerFromPrev = centerDistance * prevSlope + prevPoint.position;
		float centerFromNext = -centerDistance * nextSlope + nextPoint.position;
		
		// create a bezier curve with the extrapolated center points and the existing points as anchors
		std::vector<DistortionPoint> controlPoints ={
			prevPoint,
			{prevPoint.degree + centerDistance, centerFromPrev},
			{nextPoint.degree - centerDistance, centerFromNext},
			nextPoint
		};
		
		outPoints.push_back(prevPoint);
		// generate inner points based on the bezier curve
		for(int j = 0; j < innerPointCounts; j++){
			outPoints.push_back(BezierPoint((j + 1) / static_cast<float>(innerPointCounts + 1), controlPoints));
		}
	}
	outPoints.push_back(points[points.size() - 1]);
	return outPoints;
}

// linear interpolation between two values based on a t value between 0 and 1
inline float lerp(float a, float b, float t){
	return a + t * (b - a);
}

// sample a value from the points based on the degree
float SampleFromPoints(const std::vector<DistortionPoint>& points, float degree){
	// find the two points that the degree is between
	for(int i = 0; i < points.size() - 1; i++){
		if(degree >= points[i].degree && degree <= points[i + 1].degree){
			// interpolate between the two points
			float t = (degree - points[i].degree) / (points[i + 1].degree - points[i].degree);
			return points[i].position + t * (points[i + 1].position - points[i].position);
		}
	}
	// if the degree is outside the range of the points, return the closest point
	if(degree < points[0].degree){
		return points[0].position;
	}else{
		// interpolate between the last two points
		int i = points.size() - 2;
		float t = (degree - points[i].degree) / (points[i + 1].degree - points[i].degree);
		return lerp(points[i].position, points[i + 1].position, t);
	}
}

// inverse of SampleFromPoints, returns the degree for a given position
float SampleFromPointsInverse(const std::vector<DistortionPoint>& points, float position){
	// find the two points that the position is between
	for(int i = 0; i < points.size() - 1; i++){
		if(position >= points[i].position && position <= points[i + 1].position){
			// interpolate between the two points
			float t = (position - points[i].position) / (points[i + 1].position - points[i].position);
			return points[i].degree + t * (points[i + 1].degree - points[i].degree);
		}
	}
	// if the position is outside the range of the points, return the closest point
	if(position < points[0].position){
		return points[0].degree;
	}else{
		// interpolate between the last two points
		int i = points.size() - 2;
		float t = (position - points[i].position) / (points[i + 1].position - points[i].position);
		return lerp(points[i].degree, points[i + 1].degree, t);
	}
}


// sample from float map with linear interpolation
inline float RadialBezierDistortionProfile::SampleFromMap(float* map, float radius){
	float indexFloat = radius * radialMapConversion;
	int index = (int)(indexFloat);
	if(index < 0){
		index = 0;
	}else if(index >= radialMapSize - 1){
		index = radialMapSize - 2;
	}
	return lerp(map[index], map[index + 1], indexFloat - index);
}

// compute ppd in range
float RadialBezierDistortionProfile::ComputePPD(std::vector<DistortionPoint> distortion, float degreeStart, float degreeEnd){
	// compute ppd for the given range of degrees
	return (SampleFromPoints(distortion, degreeEnd) - SampleFromPoints(distortion, degreeStart)) / (degreeEnd - degreeStart) / 100.0f * resolution / 2.0f;
}

void RadialBezierDistortionProfile::Initialize(){
	Cleanup();
	// smooth the points
	std::vector<DistortionPoint> distortionsSmoothGreen = SmoothPoints(distortions, inBetweenPoints);
	std::vector<DistortionPoint> distortionsRedPercent = SmoothPoints(distortionsRed, inBetweenPoints);
	std::vector<DistortionPoint> distortionsBluePercent = SmoothPoints(distortionsBlue, inBetweenPoints);
	
	std::vector<DistortionPoint> distortionsSmoothRed = distortionsSmoothGreen;
	std::vector<DistortionPoint> distortionsSmoothBlue = distortionsSmoothGreen;
	// correct for chromatic aberration
	for(int i = 0; i < distortionsSmoothGreen.size(); i++){
		distortionsSmoothRed[i].position *= SampleFromPoints(distortionsRedPercent, distortionsSmoothRed[i].degree) / 100.0f + 1.0f;
		distortionsSmoothBlue[i].position *= SampleFromPoints(distortionsBluePercent, distortionsSmoothBlue[i].degree) / 100.0f + 1.0f;
		halfFov = std::max(halfFov, distortionsSmoothGreen[i].degree);
	}
	
	DriverLog("PPD at 0°: %f\n", ComputePPD(distortionsSmoothGreen, 0, 1));
	DriverLog("PPD at 10°: %f\n", ComputePPD(distortionsSmoothGreen, 10, 11));
	DriverLog("PPD at 20°: %f\n", ComputePPD(distortionsSmoothGreen, 20, 21));
	DriverLog("PPD at 30°: %f\n", ComputePPD(distortionsSmoothGreen, 30, 31));
	DriverLog("PPD at 40°: %f\n", ComputePPD(distortionsSmoothGreen, 40, 41));
	
	DriverLog("PPD average 0° to 10°: %f\n", ComputePPD(distortionsSmoothGreen, 0, 10));
	DriverLog("PPD average 0° to 20°: %f\n", ComputePPD(distortionsSmoothGreen, 0, 20));
	
	// convert to input coordinates and flip the point values to sample from output to input
	float edgeTan = tan(halfFov * M_PI / 180.0f);
	for (int i = 0; i < distortionsSmoothGreen.size(); i++){
		// use tangent to convert from degrees into input screen space
		distortionsSmoothRed[i].degree = tan(distortionsSmoothRed[i].degree * M_PI / 180.0f) / edgeTan;
		distortionsSmoothGreen[i].degree = tan(distortionsSmoothGreen[i].degree * M_PI / 180.0f) / edgeTan;
		distortionsSmoothBlue[i].degree = tan(distortionsSmoothBlue[i].degree * M_PI / 180.0f) / edgeTan;
	}
	
	
	float maxInputOutputRatio = 0.0f;
	for(int i = 0; i < distortionsSmoothGreen.size() - 1; i++){
		DistortionPoint prevPoint = distortionsSmoothGreen[i];
		DistortionPoint nextPoint = distortionsSmoothGreen[i + 1];
		float inputOutputRatio = (nextPoint.position - prevPoint.position) / 100.0f / (nextPoint.degree - prevPoint.degree);
		maxInputOutputRatio = std::max(maxInputOutputRatio, inputOutputRatio);
		// DriverLog("distortion ratio: %f", inputOutputRatio);
	}
	// steamvr lists percentage as total number of pixels, not a single dimension
	DriverLog("Oversampling required for 1:1 distortion: %f%% %ix%i", (maxInputOutputRatio * maxInputOutputRatio) * 100.0f, (int)(maxInputOutputRatio * resolution), (int)(maxInputOutputRatio * resolution));
	
	if(false){
		char* distortionPointLog = new char[distortionsSmoothGreen.size() * 40];
		int distortionPointLogSize = 0;
		for(int i = 0; i < distortionsSmoothGreen.size(); i++){
			distortionPointLogSize += sprintf(distortionPointLog + distortionPointLogSize, "[%f, %f] ", distortionsSmoothBlue[i].position, distortionsSmoothBlue[i].degree);
		}
		// DriverLog("distortion points: %s", distortionPointLog);
		delete[] distortionPointLog;
	}
	
	// create radial maps
	radialUVMapR = new float[radialMapSize];
	radialUVMapG = new float[radialMapSize];
	radialUVMapB = new float[radialMapSize];
	radialMapConversion = (float)radialMapSize / 1.0f;
	for(int i = 0; i < radialMapSize; i++){
		float outputRadius = i / radialMapConversion * 100;
		radialUVMapR[i] = SampleFromPointsInverse(distortionsSmoothRed, outputRadius);
		radialUVMapG[i] = SampleFromPointsInverse(distortionsSmoothGreen, outputRadius);
		radialUVMapB[i] = SampleFromPointsInverse(distortionsSmoothBlue, outputRadius);
	}
	
	if(false){
		char* radialMapLog = new char[radialMapSize * 20];
		int radialMapLogSize = 0;
		for(int i = 200; i < radialMapSize; i++){
			radialMapLogSize += sprintf(radialMapLog + radialMapLogSize, "%f ", radialUVMapB[i]);
		}
		DriverLog("distortion radial map: %s", radialMapLog);
		delete[] radialMapLog;
	}
	
}

void RadialBezierDistortionProfile::GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight, float* pfBottom, float* pfTop){
	DriverLog("GetProjectionRaw returning an fov of %f", halfFov * 2.0f);
	float hFovHalf = halfFov;
	float vFovHalf = halfFov;
	
	hFovHalf = hFovHalf * M_PI / 180.0f;
	vFovHalf = vFovHalf * M_PI / 180.0f;
	
	*pfLeft = tan(-hFovHalf);
	*pfRight = tan(hFovHalf);
	*pfTop = tan(vFovHalf);
	*pfBottom = tan(-vFovHalf);
}

Point2D RadialBezierDistortionProfile::ComputeDistortion(vr::EVREye eEye, ColorChannel colorChannel, float fU, float fV){
	
	// convert to radius and unit vector
	float radius = sqrt(fU * fU + fV * fV);
	float unitU = fU / radius;
	float unitV = fV / radius;
	// fix NaNs
	if(unitU != unitU){
		unitU = 0;
	}
	if(unitV != unitV){
		unitV = 0;
	}
	
	// sample distortion map for the given radius and color channel
	switch (colorChannel){
		case ColorChannelRed:
			radius = SampleFromMap(radialUVMapR, radius);
			break;
		case ColorChannelGreen:
			radius = SampleFromMap(radialUVMapG, radius);
			break;
		case ColorChannelBlue:
			radius = SampleFromMap(radialUVMapB, radius);
			break;
	}
	
	// convert back to points and return
	Point2D distortion;
	distortion.x = unitU * radius;
	distortion.y = unitV * radius;
	return distortion;
}

void RadialBezierDistortionProfile::Cleanup(){
	if(radialUVMapR != nullptr){
		delete[] radialUVMapR;
		radialUVMapR = nullptr;
	}
	if(radialUVMapG != nullptr){
		delete[] radialUVMapG;
		radialUVMapG = nullptr;
	}
	if(radialUVMapB != nullptr){
		delete[] radialUVMapB;
		radialUVMapB = nullptr;
	}
}

RadialBezierDistortionProfile::~RadialBezierDistortionProfile(){
	Cleanup();
}