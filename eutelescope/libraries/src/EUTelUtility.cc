/*
 * File:   EUTelUtility.cpp
 * Contact: denys.lontkovskyi@desy.de
 *
 * Created on January 22, 2013, 11:54 AM
 */

// eutelescope includes ".h"
#include "EUTelUtility.h"
#include "EUTELESCOPE.h"
#include "EUTelBrickedClusterImpl.h"
#include "EUTelDFFClusterImpl.h"
#include "EUTelFFClusterImpl.h"
#include "EUTelSparseClusterImpl.h"
#include "EUTelSparseClusterImpl.h"
#include "EUTelVirtualCluster.h"

// lcio includes <.h>
#include <EVENT/LCEvent.h>

#include <cstdio>

using namespace std;

namespace eutelescope {

  namespace Utility {

    // Cantor pairing function
    int cantorEncode(int X, int Y) {
      return (X + Y) * (X + Y + 1) / 2 + Y;
    }

    /** Returns the rotation matrix for given angles
     *  Rotation order is as following: Z->X->Y, i.e. R =
     * Y(beta)*X(alpha)*Z(gamma) */
    Eigen::Matrix3d rotationMatrixFromAngles(long double alpha,
                                             long double beta,
                                             long double gamma) {
      // Eigen::IOFormat IO(6, 0, ", ", ";\n", "[", "]", "[", "]");
      // std::cout << "alpha, beta, gamma: " << alpha << ", " << beta << ", " <<
      // gamma << std::endl;
      long double cosA = cos(alpha);
      long double sinA = sin(alpha);
      long double cosB = cos(beta);
      long double sinB = sin(beta);
      long double cosG = cos(gamma);
      long double sinG = sin(gamma);

      // std::cout << "trig" << cosA << ", " << cosB << ", " << cosG << ", " <<
      // sinA << ", " << sinB << ", " << sinG <<  std::endl;

      Eigen::Matrix3d rotMat;
      rotMat << static_cast<double>(cosB * cosG + sinA * sinB * sinG),
          static_cast<double>(sinA * sinB * cosG - cosB * sinG), static_cast<double>(cosA * sinB),
          static_cast<double>(cosA * sinG), static_cast<double>(cosA * cosG), static_cast<double>(-sinA),
          static_cast<double>(sinA * cosB * sinG - sinB * cosG),
          static_cast<double>(sinA * cosB * cosG + sinB * sinG), static_cast<double>(cosA * cosB);
      // std::cout << rotMat.format(IO) << std::endl;
      return rotMat;
    }

    Eigen::Vector3d getRotationAnglesFromMatrix(Eigen::Matrix3d rotMat) {

      long double aX = 0;
      long double aY = 0;
      long double aZ = 0;

      // This is a correct decomposition for the given rotation order: YXZ, i.e.
      // initial Z-rotation,
      // followed by X and ultimately Y rotation. In the case of a gimbal lock,
      // the angle around the
      // Z axis is (arbitrarily) set to 0.
      if (rotMat(1, 2) < 1) {
        if (rotMat(1, 2) > -1) {
          aX = std::asin(-rotMat(1, 2));
          aY = std::atan2(rotMat(0, 2), rotMat(2, 2));
          aZ = std::atan2(rotMat(1, 0), rotMat(1, 1));
        } else /* r12 = -1 */ {
          aX = PI / 2;
          aY = -std::atan2(-rotMat(0, 1), rotMat(0, 0));
          aZ = 0;
        }
      } else /* r12 = 1 */ {
        aX = -PI / 2;
        aY = std::atan2(-rotMat(0, 1), rotMat(0, 0));
        aZ = 0;
      }

      Eigen::Vector3d vec;
      vec << static_cast<double>(aX), static_cast<double>(aY), static_cast<double>(aZ);
      return vec;
    }

    std::map<int, std::vector<int>>
    readNoisyPixelList(LCEvent *event,
                       std::string const &noisyPixelCollectionName) {

      // Preapare pointer to hot pixel collection
      LCCollectionVec *noisyPixelCollectionVec = nullptr;

      // Try to obtain the collection
      try {
        noisyPixelCollectionVec = static_cast<LCCollectionVec *>(
            event->getCollection(noisyPixelCollectionName));
      } catch (...) {
        if (!noisyPixelCollectionName.empty()) {
          streamlog_out(WARNING1) << "noisyPixelCollectionName "
                                  << noisyPixelCollectionName.c_str()
                                  << " not found" << std::endl;
          streamlog_out(WARNING1)
              << "READ CAREFULLY: This means that no noisy pixels will be "
                 "removed, despite the processor successfully running!"
              << std::endl;
        }
        return std::map<int, std::vector<int>>();
      }

      // Decoder to get sensor ID
      CellIDDecoder<TrackerDataImpl> cellDecoder(noisyPixelCollectionVec);
      auto pixel = std::unique_ptr<EUTelBaseSparsePixel>(nullptr);
      std::map<int, std::vector<int>> noisyPixelMap;

      // Loop over all hot pixels
      for (int i = 0; i < noisyPixelCollectionVec->getNumberOfElements(); i++) {
        // Get the TrackerData for the sensor ID
        TrackerDataImpl *noisyPixelData = dynamic_cast<TrackerDataImpl *>(
            noisyPixelCollectionVec->getElementAt(i));
        int sensorID = static_cast<int>(cellDecoder(noisyPixelData)["sensorID"]);
        int pixelType = static_cast<int>(cellDecoder(noisyPixelData)["sparsePixelType"]);

        // And get the corresponding noise vector for that plane
        std::vector<int> *noiseSensorVector = &(noisyPixelMap[sensorID]);
        std::unique_ptr<EUTelTrackerDataInterfacer> noisyPixelDataInterface;

        if (pixelType == kEUTelGenericSparsePixel) {
          auto noisyPixelDataInterface = std::make_unique<
              EUTelTrackerDataInterfacerImpl<EUTelGenericSparsePixel>>(
              noisyPixelData);
          auto &pixelVec = noisyPixelDataInterface->getPixels();

          for (auto &pixel : pixelVec) {
            noiseSensorVector->push_back(
                cantorEncode(pixel.getXCoord(), pixel.getYCoord()));
          }
        } else {
          streamlog_out(ERROR5)
              << "The noisy pixel collection is corrupted, it does not contain "
                 "the right pixel type. Something is wrong!"
              << std::endl;
        }
      }
      for (std::map<int, std::vector<int>>::iterator it = noisyPixelMap.begin();
           it != noisyPixelMap.end(); ++it) {
        // Sort the noisy pixel maps
        std::sort((it->second).begin(), (it->second).end());
        streamlog_out(MESSAGE5) << "Read in " << (it->second).size()
                                << " noisy pixels on plane " << (it->first)
                                << std::endl;
      }
      return noisyPixelMap;
    }

    std::unique_ptr<EUTelTrackerDataInterfacer>
    getSparseData(IMPL::TrackerDataImpl *const data, int type) {
      return getSparseData(data, static_cast<SparsePixelType>(type));
    }

    std::unique_ptr<EUTelClusterDataInterfacerBase>
    getClusterData(IMPL::TrackerDataImpl *const data, int type) {
      return getClusterData(data, static_cast<SparsePixelType>(type));
    }

    std::unique_ptr<EUTelClusterDataInterfacerBase>
    getClusterData(IMPL::TrackerDataImpl *const data, SparsePixelType type) {
      switch (type) {
      case kEUTelSimpleSparsePixel:
        return std::unique_ptr<EUTelClusterDataInterfacerBase>(
            new EUTelSparseClusterImpl<EUTelSimpleSparsePixel>(data));
      case kEUTelGenericSparsePixel:
        return std::unique_ptr<EUTelClusterDataInterfacerBase>(
            new EUTelSparseClusterImpl<EUTelGenericSparsePixel>(data));
      case kEUTelGeometricPixel:
        return std::unique_ptr<EUTelClusterDataInterfacerBase>(
            new EUTelSparseClusterImpl<EUTelGeometricPixel>(data));
      case kEUTelMuPixel:
        return std::unique_ptr<EUTelClusterDataInterfacerBase>(
            new EUTelSparseClusterImpl<EUTelMuPixel>(data));
      default:
        throw UnknownDataTypeException("Unknown sparsified pixel");
      }
    }

    std::unique_ptr<EUTelTrackerDataInterfacer>
    getSparseData(IMPL::TrackerDataImpl *const data, SparsePixelType type) {
      switch (type) {
      case kEUTelSimpleSparsePixel:
        return std::unique_ptr<EUTelTrackerDataInterfacer>(
            new EUTelTrackerDataInterfacerImpl<EUTelSimpleSparsePixel>(data));
      case kEUTelGenericSparsePixel:
        return std::unique_ptr<EUTelTrackerDataInterfacer>(
            new EUTelTrackerDataInterfacerImpl<EUTelGenericSparsePixel>(data));
      case kEUTelGeometricPixel:
        return std::unique_ptr<EUTelTrackerDataInterfacer>(
            new EUTelTrackerDataInterfacerImpl<EUTelGeometricPixel>(data));
      case kEUTelMuPixel:
        return std::unique_ptr<EUTelTrackerDataInterfacer>(
            new EUTelTrackerDataInterfacerImpl<EUTelMuPixel>(data));
      default:
        throw UnknownDataTypeException("Unknown sparsified pixel");
      }
    }

    /** This function will set the
    * @param mat input with arbitrary precision
    * @param pre precision to set the new matrix to  */
    TMatrixD setPrecision(TMatrixD mat, double mod) {
      for (int i = 0; i < mat.GetNrows(); i++) {
        for (int j = 0; j < mat.GetNcols(); j++) {
          if (abs(mat[j][i]) < mod) {
            mat[j][i] = 0;
          }
        }
      }
      return mat;
    }

    /**
     * Fills indices of not excluded planes
     *
     *
     *      *               *
     * |    |       ...     |       |
     * 0    1       ...     k      k+1
     *
     * Plane marked with (*) to be excluded from consideration
     *
     *      *                               *
     * |    |       |       |       ...     |       |
     * 0   -1       1       2       ...    -1      k-2
     *
     * Array of indices of not excluded planes
    & *
     * @param indexconverter
     *              returned indices of not excluded planes
     *
     * @param excludePlanes
     *              array of plane ids to be excluded
     *
     * @param nPlanes
     *              total number of planes
     */
    void
    FillNotExcludedPlanesIndices(std::vector<int> &indexconverter,
                                 const std::vector<unsigned int> &excludePlanes,
                                 unsigned int nPlanes) {
      int icounter = 0;
      int nExcludePlanes = static_cast<int>(excludePlanes.size());
      for (unsigned int i = 0; i < nPlanes; i++) {
        int excluded = 0; // 0 - not excluded, 1 - excluded
        if (nExcludePlanes > 0) {
          for (int j = 0; j < nExcludePlanes; j++) {
            if (i == excludePlanes[j]) {
              excluded = 1;
              break;
            }
          }
        }
        if (excluded == 1)
          indexconverter[i] = -1;
        else {
          indexconverter[i] = icounter;
          icounter++;
        }
      }
      streamlog_out(DEBUG) << "FillNotExcludedPlanesIndices" << std::endl;
    }

    bool HitContainsHotPixels(const IMPL::TrackerHitImpl *hit,
                              const std::map<std::string, bool> &hotPixelMap) {
      bool skipHit = false;

      try {
        try {
          LCObjectVec clusterVector = hit->getRawHits();

          EUTelVirtualCluster *cluster = nullptr;

          if (hit->getType() == kEUTelSparseClusterImpl) {

            TrackerDataImpl *clusterFrame =
                dynamic_cast<TrackerDataImpl *>(clusterVector[0]);
            if (clusterFrame == nullptr) {
              // found invalid result from cast
              throw UnknownDataTypeException(
                  "Invalid hit found in method hitContainsHotPixels()");
            }

            auto cluster = std::make_unique<
                EUTelSparseClusterImpl<EUTelGenericSparsePixel>>(clusterFrame);
            int sensorID = cluster->getDetectorID();
            auto &pixelVec = cluster->getPixels();

            for (auto &m26Pixel : pixelVec) {
              char ix[100];
              sprintf(ix, "%d,%d,%d", sensorID, m26Pixel.getXCoord(),
                      m26Pixel.getYCoord());
              std::map<std::string, bool>::const_iterator z =
                  hotPixelMap.find(ix);
              if (z != hotPixelMap.end() && hotPixelMap.at(ix) == true) {
                skipHit = true;
                streamlog_out(DEBUG3)
                    << "Skipping hit as it was found in the hot pixel map."
                    << std::endl;
                break;
                //                                    delete cluster;
                //                                    return true; // if TRUE
                //                                    this hit will be skipped
              } else {
                skipHit = false;
              }
            }
          } else if (hit->getType() == kEUTelBrickedClusterImpl) {

            // fixed cluster implementation. Remember it
            //  can come from
            //  both RAW and ZS data

            cluster = new EUTelBrickedClusterImpl(
                static_cast<TrackerDataImpl *>(clusterVector[0]));
            delete cluster;
          } else if (hit->getType() == kEUTelDFFClusterImpl) {

            // fixed cluster implementation. Remember it can come from
            // both RAW and ZS data
            cluster = new EUTelDFFClusterImpl(
                static_cast<TrackerDataImpl *>(clusterVector[0]));
            delete cluster;
          } else if (hit->getType() == kEUTelFFClusterImpl) {

            // fixed cluster implementation. Remember it can come from
            // both RAW and ZS data
            cluster = new EUTelFFClusterImpl(
                static_cast<TrackerDataImpl *>(clusterVector[0]));
            delete cluster;
          }

          //                if ( cluster != 0 ) delete cluster;

        } catch (lcio::Exception & e) {
          // catch specific exceptions
          streamlog_out(ERROR)
              << "Exception occured in hitContainsHotPixels(): " << e.what()
              << std::endl;
        }
      } catch (...) {
        // if anything went wrong in the above return FALSE, meaning do not skip
        // this hit
        return 0;
      }

      // if none of the above worked return FALSE, meaning do not skip this hit
      return skipHit;
    }

    /**
     * Provides access to raw cluster information for given hit
     * Constructed object is owned by caller. Cluster must be destroyed by
     * caller.
     *
     * @param hit
     * @return raw data cluster information
     */
    std::unique_ptr<EUTelVirtualCluster>
    GetClusterFromHit(const IMPL::TrackerHitImpl *hit) {
      LCObjectVec clusterVector = hit->getRawHits();

      if (hit->getType() == kEUTelBrickedClusterImpl) {
        return std::make_unique<EUTelBrickedClusterImpl>(
            static_cast<TrackerDataImpl *>(clusterVector[0]));
      } else if (hit->getType() == kEUTelDFFClusterImpl) {
        return std::make_unique<EUTelDFFClusterImpl>(
            static_cast<TrackerDataImpl *>(clusterVector[0]));
      } else if (hit->getType() == kEUTelFFClusterImpl) {
        return std::make_unique<EUTelFFClusterImpl>(
            static_cast<TrackerDataImpl *>(clusterVector[0]));
      } else if (hit->getType() == kEUTelSparseClusterImpl) {
        return std::make_unique<
            EUTelSparseClusterImpl<EUTelGenericSparsePixel>>(
            static_cast<TrackerDataImpl *>(clusterVector[0]));
      } else {
        streamlog_out(WARNING2) << "Unknown cluster type: " << hit->getType()
                                << std::endl;
        // Not sure this is a good idea
        return std::unique_ptr<EUTelVirtualCluster>();
      }
    }

    /**
     * Determine hit's plane id
     * @param hit
     * @return plane id
     */
    int getSensorIDfromHit(EVENT::TrackerHit *hit) {
      if (hit == nullptr) {
        streamlog_out(ERROR) << "getSensorIDfromHit:: An invalid hit pointer "
                                "supplied! will exit now\n"
                             << std::endl;
        return -1;
      }

      try {
        UTIL::CellIDDecoder<TrackerHitImpl> hitDecoder(EUTELESCOPE::HITENCODING);
        int sensorID = static_cast<int>(hitDecoder(static_cast<IMPL::TrackerHitImpl*>(hit))["sensorID"]);
        return sensorID;

      } catch (...) {
        streamlog_out(ERROR) << "getSensorIDfromHit() produced an exception!"
                             << std::endl;
      }
      return -1;
    }

    std::map<std::string, bool>
    FillHotPixelMap(EVENT::LCEvent *event,
                    const std::string &hotPixelCollectionName) {

      std::map<std::string, bool> hotPixelMap;

      LCCollectionVec *hotPixelCollectionVec = nullptr;
      try {
        hotPixelCollectionVec = static_cast<LCCollectionVec *>(
            event->getCollection(hotPixelCollectionName));
      } catch (...) {
        streamlog_out(MESSAGE4) << "hotPixelCollectionName "
                                << hotPixelCollectionName.c_str()
                                << " not found" << std::endl;
        return hotPixelMap;
      }

      CellIDDecoder<TrackerDataImpl> cellDecoder(hotPixelCollectionVec);

      for (int i = 0; i < hotPixelCollectionVec->getNumberOfElements(); i++) {
        TrackerDataImpl *hotPixelData = dynamic_cast<TrackerDataImpl *>(
            hotPixelCollectionVec->getElementAt(i));
        SparsePixelType type = static_cast<SparsePixelType>(
            static_cast<int>(cellDecoder(hotPixelData)["sparsePixelType"]));

        int sensorID = static_cast<int>(cellDecoder(hotPixelData)["sensorID"]);

        if (type == kEUTelGenericSparsePixel) {
          auto m26Data =
              std::make_unique<EUTelSparseClusterImpl<EUTelGenericSparsePixel>>(
                  hotPixelData);
          auto &pixelVec = m26Data->getPixels();

          for (auto &m26Pixel : pixelVec) {
            std::vector<int> m26ColVec();
            streamlog_out(DEBUG0) << "Size: " << m26Data->size()
                                  << " HotPixelInfo:  " << m26Pixel.getXCoord()
                                  << " " << m26Pixel.getYCoord() << " "
                                  << m26Pixel.getSignal() << std::endl;
            try {
              char ix[100];
              sprintf(ix, "%d,%d,%d", sensorID, m26Pixel.getXCoord(),
                      m26Pixel.getYCoord());
              hotPixelMap[ix] = true;
            } catch (...) {
              std::cout << "can not add pixel " << std::endl;
              std::cout << sensorID << " " << m26Pixel.getXCoord() << " "
                        << m26Pixel.getYCoord() << " " << std::endl;
            }
          }
        }
      }
      return hotPixelMap;
    }

    /** Highland's formula for multiple scattering
     * @param p momentum of the particle [GeV/c]
     * @param x thickness of the material in units of radiation lenght
     */
    // The highland formula correction must be calculated for the whole
    // telescope system.
    double getThetaRMSHighland(double p, double x) {
      double tet = ((0.0136 * sqrt(x)) / p) * (1.0 + 0.038 * std::log(x));
      return tet;
    }

    /** Calculate median
     * Sort supplied vector and determine the median
     */
    double getMedian(std::vector<double> &vec) {
      std::sort(vec.begin(), vec.end());
      double median = -999.;
      size_t size = vec.size();
      if (size % 2 == 0) {
        median = (vec[size / 2 - 1] + vec[size / 2]) / 2;
      } else {
        median = vec[size / 2];
      }
      return median;
    }

    /**
     * Calculate 2D curvature of the track with given pt and charge q
     * in solenoidal magnetic field with strength B
     * @param pt transverse momentum of the track [GeV/c]
     * @param B  magnetic field strength [T]
     * @param q  particle charge [e]
     * @return 1/R 2D curvature of the track [1/m]
     */
    double getCurvature(double pt, double B, double q) {
      double rho = 0.;
      if (pt > 0.)
        rho = 0.299792458 * q * B / pt;

      return rho;
    }

    /**
     *  Solves quadratic equation a*x^2 + b*x + c = 0
     * @param a
     * @param b
     * @param c
     * @return vector of solution sorted in descending order
     */
    vector<double> solveQuadratic(double a, double b, double c) {
      //		streamlog_out( DEBUG1 ) << "Solving quadratic equation
      //with coefficients:\na: "
      //		<< a << "\nb: " << b << "\nc:" << c << std::endl;
      // Solutions
      vector<double> X; // initialise with two doubles equal 0.

      if (fabs(a) > 1.E-10) {
        // The equation has the form
        // a*x^2 + b*x + c = 0
        double disc2 = b * b - 4. * a * c;
        if (disc2 < 0.) {
          cout << " Quadratic equation solution is imaginary! WARNING! disc2 < "
                  "0: "
               << disc2 << endl;
          return X;
        }
        double disc = sqrt(disc2);
        double denom = 2. * a;
        double num1 = -b + disc;
        double num2 = -b - disc;

        X.push_back(num1 / denom); // larger root
        X.push_back(num2 / denom); // smaller root
        // X[0] = num1 / denom;            // bigger root
        // X[1] = num2 / denom;            // lower root
      } else {
        // Degenerate case, when a = 0.
        // The linear equation has the form
        // b*x + c = 0

        X.push_back(-c / b);
        X.push_back(-c / b);
        // X[0] = -c/b;
        // X[1] = -c/b;
      }

      return X;
    }

    /** get cluster size in X and Y for TrackerHit derived hits:
     *  includes only certain type of data
     *  to-do: replace with generic hit and cluster structure not requiring
     * explicit declaration of data types
     */

    void getClusterSize(const IMPL::TrackerHitImpl *hit, int &sizeX,
                        int &sizeY) {
      if (!hit) {
        streamlog_out(ERROR5)
            << "An invalid hit pointer supplied! will exit now\n"
            << endl;
        return;
      }
      std::unique_ptr<EUTelVirtualCluster> cluster = GetClusterFromHit(hit);
      if (cluster.get())
        cluster->getClusterSize(sizeX, sizeY);
    }

    void copyLCCollectionHitVec(IMPL::LCCollectionVec *input,
                                LCCollectionVec *output) {

      // Prepare output collection
      //	 	LCFlagImpl flag( input->getFlag() );
      //	  	flag.setBit( LCIO::TRBIT_HITS );
      //	  	output->setFlag( flag.getFlag( ) );

      // deep copy of all elements  - requires clone of original elements
      //
      int nElements = input->getNumberOfElements();

      streamlog_out(DEBUG4)
          << "HIT : copy of n= " << nElements
          << " element for collection : " << input->getTypeName() << std::endl;

      for (int i = 0; i < nElements; i++) {
        IMPL::TrackerHitImpl *hit =
            static_cast<IMPL::TrackerHitImpl *>(input->getElementAt(i));
        streamlog_out(DEBUG4) << " i= " << i << " type : " << hit->getType()
                              << std::endl;
        output->push_back(hit);
      }
    }

    // Create once per event     !!
    void copyLCCollectionTrackVec(IMPL::LCCollectionVec *input,
                                  LCCollectionVec *output) {

      // Prepare output collection
      LCFlagImpl flag(input->getFlag());
      flag.setBit(LCIO::TRBIT_HITS);
      output->setFlag(flag.getFlag());

      // deep copy of all elements  - requires clone of original elements
      //
      int nElements = input->getNumberOfElements();

      streamlog_out(DEBUG4)
          << "TRACK: copy of n= " << nElements
          << " element for collection : " << input->getTypeName() << std::endl;

      for (int i = 0; i < nElements; i++) {
        IMPL::TrackImpl *trk =
            static_cast<IMPL::TrackImpl *>(input->getElementAt(i));
        streamlog_out(DEBUG4) << " i= " << i << " type : " << trk->getType()
                              << " nstates: " << trk->getTrackStates().size()
                              << " nhits: " << trk->getTrackerHits().size()
                              << std::endl;
        output->push_back(trk);
        //	     		output->push_back(  trk->clone() ) ;
      }
    }



	void copyLCCollectionParameters(LCCollectionVec* to, LCCollectionVec const * const from){
		auto& paramsFrom = from->getParameters();
		auto& paramsTo = to->parameters();
		
		EVENT::StringVec intKeyVec;
		EVENT::StringVec floatKeyVec;
		EVENT::StringVec stringKeyVec;
		
		for(auto& key: paramsFrom.getIntKeys(intKeyVec)){
			EVENT::IntVec vec;
			paramsFrom.getIntVals(key, vec);
			paramsTo.setValues(key, vec);			 
		}
		for(auto& key: paramsFrom.getFloatKeys(floatKeyVec)){
			EVENT::FloatVec vec;
			paramsFrom.getFloatVals(key, vec);
			paramsTo.setValues(key, vec);			 
		}
		for(auto& key: paramsFrom.getStringKeys(stringKeyVec)){
			EVENT::StringVec vec;
			paramsFrom.getStringVals(key, vec);
			paramsTo.setValues(key, vec);			 
		}
	}

    float DoubleToFloat(double a) { return static_cast<float>(a); }

    inline float* toFloatN(double *a, int N) {
      float *vec = new float[N];
      for (int i = 0; i < N; i++) {
        vec[i] = DoubleToFloat(a[i]);
      }
      return vec;
    }

    const float *HitCDoubleShiftCFloat(const double *hitPosition,
                                       TVectorD &residual) {

      double hit[3];
      std::copy(hitPosition, hitPosition + 3, hit);

      hit[0] -= residual[0]; // why minus?
      hit[1] -= residual[1];

      streamlog_out(MESSAGE1) << " " << hit[0] << " " << hit[1] << " " << hit[2]
                              << std::endl;

      float *opoint = new float[3];

      // c++0x     std::ransform( hitPointLocal, hitPointLocal+3,
      // const_cast<float*>( temp), [](double hitPointLocal){return (float)
      // hitPointLocal;}  );
      std::transform(hit, hit + 3, opoint, DoubleToFloat);
      return opoint;
    }
  }
}
