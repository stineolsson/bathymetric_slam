/* Copyright 2019 Ignacio Torroba (torroba@kth.se)
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "submaps_tools/submaps.hpp"
#include <iomanip>

using namespace Eigen;

MapObj::MapObj(){

}

MapObj::MapObj(PointCloudT& map_pcl){

}


SubmapObj::SubmapObj(){

    submap_info_ = createDRWeights();
}

SubmapObj::SubmapObj(const unsigned int& submap_id, const unsigned int& swath_id, PointCloudT& submap_pcl):
            submap_id_(submap_id), swath_id_(swath_id), submap_pcl_(submap_pcl){

    // AUV pose estimate while acquiring submap
    submap_tf_ = (Eigen::Isometry3f) submap_pcl.sensor_orientation_;
    submap_tf_.translation() = Eigen::Vector3f(submap_pcl.sensor_origin_.head(3));

    // Delete sensor pose in pcl and transform the points accordingly
    submap_pcl_.sensor_origin_ << 0.0,0.0,0.0,0.0;
    submap_pcl_.sensor_orientation_ = Eigen::Quaternionf(0,0,0,0);
    pcl::transformPointCloud(submap_pcl_, submap_pcl_, submap_tf_.matrix());

    submap_info_ = createDRWeights();
}

Eigen::Matrix<double, 6, 6> SubmapObj::createDRWeights(){

    // Uncertainty on vehicle nav across submaps (assuming here that each has a similar length)
    std::vector<double> noiseTranslation;
    std::vector<double> noiseRotation;
    noiseTranslation.push_back(3);
    noiseTranslation.push_back(3);
    noiseTranslation.push_back(0.0001);
    noiseRotation.push_back(0.0001);
    noiseRotation.push_back(0.0001);
    noiseRotation.push_back(0.1);

    Eigen::Matrix3d transNoise = Eigen::Matrix3d::Zero();
    for (int i = 0; i < 3; ++i)
      transNoise(i, i) = std::pow(noiseTranslation[i], 2);

    Eigen::Matrix3d rotNoise = Eigen::Matrix3d::Zero();
    for (int i = 0; i < 3; ++i)
      rotNoise(i, i) = std::pow(noiseRotation[i], 2);

    // Information matrix of the distribution
    Eigen::Matrix<double, 6, 6> information = Eigen::Matrix<double, 6, 6>::Zero();
    information.block<3,3>(0,0) = transNoise.inverse();
    information.block<3,3>(3,3) = rotNoise.inverse();

//    std::cout << information << std::endl;
    return information;
}

void SubmapObj::findOverlaps(std::vector<SubmapObj, Eigen::aligned_allocator<SubmapObj> > &submaps_set){

    overlaps_idx_.clear();

    std::vector<std::pair<int, corners>> corners_set;
    corners submap_i_corners = std::get<1>(getSubmapCorners(*this));
    // Extract corners of all submaps
    for(SubmapObj& submap_j: submaps_set){
        corners_set.push_back(getSubmapCorners(submap_j));
    }

    bool overlap_flag;
    for(unsigned int k=0; k<corners_set.size(); k++){
        overlap_flag = false;
        // Check each corner of submap_j against the four edges of submap_i
        overlap_flag = checkSubmapsOverlap(submap_i_corners, std::get<1>(corners_set.at(k)));
        if(overlap_flag == true){
            overlaps_idx_.push_back(std::get<0>(corners_set.at(k)));
        }
    }
}


std::pair<int, corners> getSubmapCorners(const SubmapObj& submap){

    // Transform point cloud back to map frame
    PointCloudT submap_pcl_aux;
    pcl::transformPointCloud(submap.submap_pcl_, submap_pcl_aux, submap.submap_tf_.inverse().matrix());

    // Extract corners
    Eigen::MatrixXf points = submap_pcl_aux.getMatrixXfMap(3,4,0).transpose();
    double min_x, min_y, max_x, max_y;
    double overlap_coverage = 0.6; // Reduce submap area to look for overlap by this factor
    min_x = points.col(0).minCoeff() * overlap_coverage;   // min x
    min_y = points.col(1).minCoeff() * overlap_coverage;   // min y
    max_x = points.col(0).maxCoeff() * overlap_coverage;   // max x
    max_y = points.col(1).maxCoeff() * overlap_coverage;   // max y

    // 2D transformation of the corners back to original place
//    Eigen::Isometry2d submap_tf2d = (Eigen::Isometry2d) submap.submap_tf_.linear().cast<double>();
//    submap_tf2d.translation() = submap.submap_tf_.matrix().block<2,1>(0,3).cast<double>();

    corners submap_i_corners;
    submap_i_corners.push_back(submap.submap_tf_.cast<double>() * Vector3d(min_x, min_y, 0));
    submap_i_corners.push_back(submap.submap_tf_.cast<double>() * Vector3d(min_x, max_y, 0));
    submap_i_corners.push_back(submap.submap_tf_.cast<double>() * Vector3d(max_x, max_y, 0));
    submap_i_corners.push_back(submap.submap_tf_.cast<double>() * Vector3d(max_x, min_y, 0));

    return std::make_pair(submap.submap_id_, submap_i_corners);
}


bool checkSubmapsOverlap(const corners submap_i_corners, const corners submap_k_corners){

    // Check every corner of i against every edge of k
    int inside;
    bool overlap = false;
    unsigned int k_next;
    for(Vector3d corner_i: submap_i_corners){
        if(overlap == true){
            break;
        }
        inside = 0;
        for(unsigned int k = 0; k<submap_k_corners.size(); k++){
            // Four corners
            k_next = k + 1;
            k_next = (k_next == submap_k_corners.size())? 0: k_next;
            // Check against four edges
            if(pointToLine(submap_k_corners.at(k), submap_k_corners.at(k_next), corner_i)){
                inside++;
            }
            else{
                break;
            }
        }
        overlap = (inside == 4)? true: false;
    }
    return overlap;
}

// Segment goes a --> b
bool pointToLine(const Vector3d seg_a, const Vector3d seg_b, const Vector3d point_c){

    int s = (seg_b[1] - seg_a[1]) * point_c[0] + (seg_a[0] - seg_b[0]) * point_c[1] + (seg_b[0] * seg_a[1] - seg_a[0] * seg_b[1]);

    return (s > 0)? true: false; // Point on right side
}


void readSubmapFile(const string submap_str, PointCloudT::Ptr submap_pcl){

    if (pcl::io::loadPCDFile<pcl::PointXYZ> (submap_str, *submap_pcl) == -1){
        PCL_ERROR ("Couldn't read .pcd file \n");
    }
}


std::vector<std::string> checkFilesInDir(DIR *dir){
    // Check files and directories within directory
    struct dirent *ent;
    std::vector<std::string> files;
    while ((ent = readdir (dir)) != NULL) {
        if( ent->d_type != DT_DIR ){
            // If directory, move on
            files.push_back(std::string(ent->d_name));
        }
    }
    closedir(dir);
    return files;
}


std::vector<SubmapObj, Eigen::aligned_allocator<SubmapObj>> readSubmapsInDir(const string& dir_path){

    std::vector<SubmapObj, Eigen::aligned_allocator<SubmapObj>> submaps_set;
    DIR *dir;
    if ((dir = opendir(dir_path.c_str())) != NULL) {
        // Open directory and check all files inside
        std::vector<std::string> files = checkFilesInDir(dir);
        std::sort(files.begin(), files.end());

        PointCloudT::Ptr submap_ptr (new PointCloudT);
        // For every file in the dir
        int submap_cnt = 0;
        int swath_cnt = 0;
        double prev_direction = 0;
        Eigen::Vector3f euler;
        for(const std::string file: files){
            string file_name = std::string(dir_path) + file;
            std::cout << "Reading file: " << file_name << std::endl;
            readSubmapFile(file_name, submap_ptr);
            // Update swath counter
            euler = submap_ptr->sensor_orientation_.toRotationMatrix().eulerAngles(2, 1, 0);
            if(abs(euler[2] - prev_direction) > M_PI/2 /*&& euler[0]>0.0001*/){
                swath_cnt = swath_cnt + 1;
                prev_direction = euler[2];
            }
            SubmapObj submap_i(submap_cnt, swath_cnt, *submap_ptr);

            // Add AUV track to submap object
            submap_i.auv_tracks_.conservativeResize(submap_i.auv_tracks_.rows()+1, 3);
            submap_i.auv_tracks_.row(0) = submap_i.submap_tf_.translation().transpose().cast<double>();
            submaps_set.push_back(submap_i);
            submap_cnt ++;
         }
    }
    return submaps_set;
}


PointsT pclToMatrixSubmap(const SubmapsVec& submaps_set){

    PointsT submaps;
    for(const SubmapObj& submap: submaps_set){
        Eigen::MatrixXf points_submap_i = submap.submap_pcl_.getMatrixXfMap(3,4,0).transpose();
        submaps.push_back(points_submap_i.cast<double>());
    }

    return submaps;
}

PointsT trackToMatrixSubmap(const SubmapsVec& submaps_set){

    PointsT tracks;
    for(const SubmapObj& submap: submaps_set){
        tracks.push_back(submap.auv_tracks_);
    }

    return tracks;
}


Eigen::Array3f computeInfoInSubmap(const SubmapObj& submap){

    // Beams z centroid
    Eigen::Array3f mean_beam;
    float beam_cnt = 0.0;
    mean_beam.setZero();
    for (const PointT& beam: submap.submap_pcl_){
        mean_beam += beam.getArray3fMap();
        beam_cnt ++;
    }
    mean_beam = mean_beam / beam_cnt;

    // Condition number of z
    Eigen::Array3f cond_num;
    cond_num.setZero();
    for (const PointT& beam: submap.submap_pcl_){
        cond_num += (beam.getArray3fMap() - mean_beam).abs();
    }

    return cond_num;
}

SubmapsVec parsePingsAUVlib(std_data::mbes_ping::PingsT& pings, const Eigen::Isometry3d& map_tf){

    SubmapsVec pings_subs;
    std::cout << std::fixed;
    std::cout << std::setprecision(10);

    // For every .all file
    Isometry3d submap_tf;
    int cnt = 0;
    for (auto pos = pings.begin(); pos != pings.end(); ) {
        auto next = std::find_if(pos, pings.end(), [&](const std_data::mbes_ping& ping) {
            return ping.first_in_file_ && (&ping != &(*pos));
        });
        std_data::mbes_ping::PingsT track_pings;

        track_pings.insert(track_pings.end(), pos, next);
        if (pos == next) {
            break;
        }

        // get the direction of the submap as the mean direction
        Vector3d ang;
        Vector3f pitch;
        Eigen::Matrix3d RM;

        int ping_cnt =0;
        // For every ping in the .all file
        for (std_data::mbes_ping& ping : track_pings) {
            ping_cnt ++;
            SubmapObj ping_sub;
            int beam_cnt = 0;
            for (Vector3d& p : ping.beams) {
                if(ping.beams.size()< 130){
                    beam_cnt++;
                }
                p -= map_tf.translation();
                ping_sub.submap_pcl_.points.push_back(PointT(p.x(), p.y(), p.z()));
            }
            ping_sub.submap_tf_.translation() = (ping.pos_- map_tf.translation()).cast<float>();
            Vector3d dir = ping.beams.back() - ping.beams.front();

            ang << ping.roll_, ping.pitch_, 0;
            ang[2] = std::atan2(dir(1), dir(0)) + M_PI/2.0;
            RM = data_transforms::euler_to_matrix(ang(0), ang(1), ang(2));
            ping_sub.submap_tf_.linear() = RM.cast<float>();
            pings_subs.push_back(ping_sub);
        }
        pos = next;
        cnt++;
    }
    return pings_subs;
}

SubmapsVec parseSubmapsAUVlib(std_data::pt_submaps& ss){

    SubmapsVec submaps_set;
    std::cout << std::fixed;
    std::cout << std::setprecision(10);
    int swath_cnt =0;
    int swath_cnt_prev = 0;
    std::cout << "Number of submaps " << ss.points.size() << std::endl;

    int submap_id = 0;
    Isometry3d map_tf;
    Isometry3d submap_tf;
    for(unsigned int k=0; k<ss.points.size(); k++){
        SubmapObj submap_k;
        submap_k.submap_id_ = submap_id++;
        swath_cnt_prev = swath_cnt;

        if(k>0){
            if(std::norm(ss.angles.at(k)[2] - ss.angles.at(k-1)[2]) > M_PI){
                ++swath_cnt;
            }
        }
        submap_k.swath_id_ = swath_cnt;

        // Apply original transform to points and vehicle track
        MatrixXd submap = ss.points.at(k);
        submap = submap * ss.rots.at(k).transpose();
        submap.array().rowwise() += ss.trans.at(k).transpose().array();

        MatrixXd tracks = ss.tracks.at(k);
        tracks = tracks * ss.rots.at(k).transpose();
        tracks.array().rowwise() += ss.trans.at(k).transpose().array();
        submap_k.auv_tracks_ = tracks;

        // Construct submap tf
        int mid = (tracks.size() % 2 == 0)? tracks.size()/2: (tracks.size()+1)/2;
        Eigen::Quaterniond rot(ss.rots.at(k));
        Eigen::Vector3d trans = ss.trans.at(k);
        trans(2) = tracks.row(mid)(1);
        submap_tf.translation() = trans;
        submap_tf.linear() = rot.toRotationMatrix();

        // Create map frame on top of first submap frame: avoid losing accuracy on floats
        if(k==0){
            map_tf = submap_tf;
            std::cout << "Map reference frame " << map_tf.translation().transpose() << std::endl;
        }
        submap.array().rowwise() -= map_tf.translation().transpose().array();
        submap_k.auv_tracks_.array().rowwise() -= map_tf.translation().transpose().array();
        submap_tf.translation().array() -= map_tf.translation().transpose().array();
        submap_k.submap_tf_ = submap_tf.cast<float>();

        // Create PCL from PointsT
        for(unsigned int i=0; i<submap.rows(); i++){
            submap_k.submap_pcl_.points.push_back(PointT(submap.row(i)[0],
                                                  submap.row(i)[1],submap.row(i)[2]));
        }
        submaps_set.push_back(submap_k);
    }
    return submaps_set;
}

std::tuple<MapObj, Eigen::Isometry3d>parseMapAUVlib(std_data::pt_submaps& ss){

    MapObj map_j;
    Isometry3d map_tf;
    Isometry3d submap_tf;
    for(unsigned int k=0; k<ss.points.size(); k++){
        // Apply original transform to points and vehicle track
        MatrixXd submap = ss.points.at(k);
        submap = submap * ss.rots.at(k).transpose();
        submap.array().rowwise() += ss.trans.at(k).transpose().array();

        MatrixXd tracks = ss.tracks.at(k);
        tracks = tracks * ss.rots.at(k).transpose();
        tracks.array().rowwise() += ss.trans.at(k).transpose().array();
        map_j.auv_tracks_ = tracks;

        // Create map frame on top of first submap frame: avoid losing accuracy on floats
        if(k==0){
            int mid = (tracks.size() % 2 == 0)? tracks.size()/2: (tracks.size()+1)/2;
            Eigen::Quaterniond rot(ss.rots.at(k));
            Eigen::Vector3d trans = ss.trans.at(k);
            trans(2) = tracks.row(mid)(1);
            map_tf.translation() = trans;
            map_tf.linear() = rot.toRotationMatrix();
            std::cout << "Map reference frame " << map_tf.translation().transpose() << std::endl;
        }
        submap.array().rowwise() -= map_tf.translation().transpose().array();
        map_j.auv_tracks_.array().rowwise() -= map_tf.translation().transpose().array();
        
        // Create PCL from PointsT
        for(unsigned int i=0; i<submap.rows(); i++){
            map_j.submap_pcl_.points.push_back(PointT(submap.row(i)[0],submap.row(i)[1],submap.row(i)[2]));
        }
    }

    return std::make_tuple(map_j, map_tf);
}

void transformSubmapObj(SubmapObj& submap, Eigen::Isometry3f& poseDRt){

    Eigen::Isometry3f submap_tf_trans;
    submap_tf_trans.matrix().Identity();
    submap_tf_trans.matrix().topLeftCorner(3,3) = submap.submap_tf_.rotation().transpose().matrix();
    submap_tf_trans.matrix().topRightCorner(3,1) = -1*(submap.submap_tf_.rotation().transpose() *
                                                       submap.submap_tf_.translation()).matrix();

    pcl::transformPointCloud(submap.submap_pcl_, submap.submap_pcl_,
                             (poseDRt * submap_tf_trans).matrix());
    submap.submap_tf_ = poseDRt;

}

bool checkSubmapSize(const SubmapObj& submap_i){
    std::pair<int, corners> submap_corners = getSubmapCorners(submap_i);
    double grid_x, grid_y;
    unsigned int k_next;
    bool reject = false;
    for(unsigned int k=0; k<2; k++){
        k_next = k + 1;
        // Check against four edges
        Eigen::Vector3d corner_i = std::get<1>(submap_corners).at(k);
        Eigen::Vector3d corner_i2 = std::get<1>(submap_corners).at(k_next);
        if(k == 0){
            grid_y = (corner_i - corner_i2).norm();
        }
        else if (k == 1){
            grid_x = (corner_i - corner_i2).norm();
        }
    }

    if(abs(grid_x/grid_y) < 0.4  || abs(grid_y/grid_x) < 0.4){
        reject = true;
    }
    return reject;
}

std::pair<Eigen::Matrix2d, Eigen::Matrix2d> readCovMatrix(const std::string& file_name){

    std::string line;
    Eigen::Matrix2d prior, posterior;
    prior.setZero(); posterior.setZero();

    std::ifstream input;
    input.open(file_name);
    if(input.fail()) {
        cout << "ERROR: Cannot open the file..." << endl;
        exit(0);
    }

    int i = 0;
    while (std::getline(input, line)){
        std::istringstream iss(line);
        double a, b;
        if (!(iss >> a >> b)) { break; } // error or end of file
        if(i<2){prior.row(i) = Eigen::Vector2d(a, b).transpose();}
        else{posterior.row(i-2) = Eigen::Vector2d(a, b).transpose();}
        i++;
    }
    return std::make_pair(prior, posterior);
}

covs readCovsFromFiles(boost::filesystem::path folder){

    typedef vector<boost::filesystem::path> v_path;
    v_path v;
    copy(boost::filesystem::directory_iterator(folder),
         boost::filesystem::directory_iterator(), back_inserter(v));
    sort(v.begin(), v.end());

    // Read covs generated from NN
    std::vector<std::string> files;
    for (v_path::const_iterator it(v.begin()), it_end(v.end()); it != it_end; ++it) {
        if (boost::filesystem::is_directory(*it)) {
            continue;
        }

        if (boost::filesystem::extension(*it) != ".txt") {
            continue;
        }
        files.push_back(it->string());
    }

    covs covs_lc(files.size());
    for (unsigned int file =  0; file < files.size(); file++) {
        std::regex regex("\\_");
        std::vector<std::string> out(
                        std::sregex_token_iterator(files.at(file).begin(), files.at(file).end(), regex, -1),
                        std::sregex_token_iterator());

        std::regex regex2("\\.");
        std::vector<std::string> out2(
                        std::sregex_token_iterator(out.back().begin(), out.back().end(), regex2, -1),
                        std::sregex_token_iterator());

        Eigen::Matrix2d prior, posterior;
        std::tie(prior, posterior) = readCovMatrix(files.at(file));
        covs_lc.at(std::stoi(out2.front())) = posterior;

        out.clear();
        out2.clear();
    }

    return covs_lc;
}


/// Read loop closures from external file
std::vector<std::vector<int> > readGTLoopClosures(string& fileName, int submaps_nb){

    std::ifstream infile(fileName);
    std::vector<std::vector<int> > overlaps(static_cast<unsigned long>(submaps_nb));

    std::string line;
    while(std::getline(infile, line)){
        vector<string> result;
        boost::split(result, line, boost::is_any_of(" "));
        vector<int> result_d(result.size());
        std::transform(result.begin(), result.end(), result_d.begin(), [](const std::string& val){
            return std::stoi(val);
        });
        overlaps.at(result_d[0]).insert(overlaps.at(result_d[0]).end(), result_d.begin()+1, result_d.end());
    }

    return overlaps;
}
