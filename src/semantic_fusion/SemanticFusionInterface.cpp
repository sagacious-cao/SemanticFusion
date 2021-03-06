/*
 * This file is part of SemanticFusion.
 *
 * Copyright (C) 2017 Imperial College London
 * 
 * The use of the code within this file and all code within files that 
 * make up the software that is SemanticFusion is permitted for 
 * non-commercial purposes only.  The full terms and conditions that 
 * apply to the code within this file are detailed within the LICENSE.txt 
 * file and at <http://www.imperial.ac.uk/dyson-robotics-lab/downloads/semantic-fusion/semantic-fusion-license/> 
 * unless explicitly stated.  By downloading this file you agree to 
 * comply with these terms.
 *
 * If you wish to use any of this code for commercial purposes then 
 * please email researchcontracts.engineering@imperial.ac.uk.
 *
 */

#include "SemanticFusionInterface.h"
#include "SemanticFusionCuda.h"
#include <utilities/Stopwatch.h>
#include <set>
#include <cmath>
#include <Eigen/Core>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <iostream>
#include <unordered_set>
#include <utility>

namespace std 
{
  template <class T>
  inline void my_hash_combine(std::size_t & seed, const T & v)
  {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2); 
  }

  template<typename S, typename T> struct hash<pair<S, T>> 
  {
    inline size_t operator()(const pair<S, T> & v) const
    {   
      size_t seed = 0;
      my_hash_combine(seed, v.first);
      my_hash_combine(seed, v.second);
      return seed;
    }   
  };  
}

// Basic algorithm for removing old items from probability table
template<typename T>
void remove_index(std::vector<T>& vector, const std::vector<int>& to_remove) {
  auto vector_base = vector.begin();
  typename std::vector<T>::size_type down_by = 0;
  for (auto iter = to_remove.cbegin(); 
       iter < to_remove.cend(); 
       iter++, down_by++)
  {
    typename std::vector<T>::size_type next = (iter + 1 == to_remove.cend() 
                                      ? vector.size() 
                                      : *(iter + 1));
    std::move(vector_base + *iter + 1, 
              vector_base + next, 
              vector_base + *iter - down_by);
  }
  vector.resize(vector.size() - to_remove.size());
}

void SemanticFusionInterface::CalculateProjectedProbabilityMap(const std::unique_ptr<ElasticFusionInterface>& map) {
  const int id_width = map->width(); // 640
  const int id_height = map->height(); //480
  const int table_height = class_probabilities_gpu_->height(); // num_classes
  const int table_width = class_probabilities_gpu_->width(); // max components
  renderProbabilityMap(map->GetSurfelIdsGpu(),id_width,id_height,
                       class_probabilities_gpu_->mutable_gpu_data(),
                       table_width,table_height,
                       rendered_class_probabilities_gpu_->mutable_gpu_data());
}

std::shared_ptr<caffe::Blob<float> > SemanticFusionInterface::get_rendered_probability() {
  return rendered_class_probabilities_gpu_;
}

std::shared_ptr<caffe::Blob<float> > SemanticFusionInterface::get_class_max_gpu() {
  return class_max_gpu_;
}

int SemanticFusionInterface::max_num_components() const {
  return max_components_;
}

// update the size of class_probabilities_gpu_, class_max_gpu_, according to the new global map from elastic_fusion
// the added surfel is initialized with uniqform probability and -1 class label 
void SemanticFusionInterface::UpdateProbabilityTable(const std::unique_ptr<ElasticFusionInterface>& map)
{
  const int new_table_width = map->GetMapSurfelCount(); // new global surfel number
  // printf("new_table_width %i\n", new_table_width);
  const int num_deleted = map->GetMapSurfelDeletedCount();
  // printf("num_deleted %i\n", num_deleted);
  const int table_width = class_probabilities_gpu_->width();  // max_components_
  // printf("table_width %i\n", table_width);

  const int table_height = class_probabilities_gpu_->height();  // num_classes_
  updateProbabilityTable(map->GetDeletedSurfelIdsGpu(),num_deleted,current_table_size_,
                    class_probabilities_gpu_->gpu_data(), table_width, table_height,
                    new_table_width, class_probabilities_gpu_buffer_->mutable_gpu_data(),
                    class_max_gpu_->gpu_data(),class_max_gpu_buffer_->mutable_gpu_data());
  // We then swap the pointers from the buffer to the other one
  class_probabilities_gpu_.swap(class_probabilities_gpu_buffer_);
  class_max_gpu_.swap(class_max_gpu_buffer_);
  current_table_size_ = new_table_width;
}

// unused function?
int SemanticFusionInterface::UpdateSurfelProbabilities(const int surfel_id, 
                                                        const std::vector<float>& class_probs) 
{
  assert(static_cast<int>(class_probabilities_.size()) > surfel_id);
  std::vector<float>& surfel_probs = class_probabilities_[surfel_id];
  assert(static_cast<int>(class_probs.size()) == num_classes_);
  assert(static_cast<int>(surfel_probs.size()) == num_classes_);
  float normalisation_denominator = 0.0;
  for (int class_id = 0; class_id < num_classes_; class_id++) {
    surfel_probs[class_id] *= class_probs[class_id];
    normalisation_denominator += surfel_probs[class_id];
  }
  float max_prob = 0.0;
  int max_class = -1;
  for (int class_id = 0; class_id < num_classes_; class_id++) {
    surfel_probs[class_id] /= normalisation_denominator;
    if (surfel_probs[class_id] >= max_prob) {
      max_prob = surfel_probs[class_id];
      max_class = class_id;
    }
  }
  if (max_prob >= colour_threshold_) {
    return max_class;
  }
  return -1;
}

// update probability values in class_probabilities_gpu_, class_max_gpu_, according to new incoming segmentations
void SemanticFusionInterface::UpdateProbabilities(std::shared_ptr<caffe::Blob<float> > probs,
                                      const std::unique_ptr<ElasticFusionInterface>& map)
{
  CHECK_EQ(num_classes_,probs->channels());
  const int id_width = map->width();  //640
  // printf("id_width: %i\n", id_width);
  const int id_height = map->height();  //480
  // printf("id_height: %i\n", id_height);
  const int prob_width = probs->width();  //224
  // printf("prob_width: %i\n", prob_width);  
  const int prob_height = probs->height();  //224
  // printf("prob_height: %i\n", prob_height);  
  const int prob_channels = probs->channels();  //14
  // printf("prob_channels: %i\n", prob_channels);
  const int map_size = class_probabilities_gpu_->width();  //3000000
  // printf("map_size: %i\n", map_size);
  
  fuseSemanticProbabilities(map->GetSurfelIdsGpu(),id_width,id_height,probs->gpu_data(),
                    prob_width,prob_height,prob_channels,
                    class_probabilities_gpu_->mutable_gpu_data(),
                    class_max_gpu_->mutable_gpu_data(),map_size);
  map->UpdateSurfelClassGpu(map_size,class_max_gpu_->gpu_data(),class_max_gpu_->gpu_data() + map_size,colour_threshold_);
  
  // For Debug: get the max probability and class label
  // const float* max_prob = class_max_gpu_->cpu_data() + max_components_;
  // const float* max_class = class_max_gpu_->cpu_data();
  // float this_max_prob=-1;
  // float this_max_class=-1;
  // for (int id = 1 ;id < current_table_size_; id++){
  //             this_max_prob = std::max(this_max_prob, max_prob[id]);
  //             this_max_class = std::max(this_max_class, max_class[id]);
      
  // }
  // std::cout<<"max_prob"<<this_max_prob<<std::endl;
  // std::cout<<"max_class"<<this_max_class<<std::endl;
}

void SemanticFusionInterface::CRFUpdate(const std::unique_ptr<ElasticFusionInterface>& map, const int iterations) {
  float* surfel_map = map->GetMapSurfelsGpu();
  // We very inefficiently allocate and clear a chunk of memory for every CRF update
  float * my_surfels = new float[current_table_size_ * 12];
  cudaMemcpy(my_surfels,surfel_map, sizeof(float) * current_table_size_ * 12, cudaMemcpyDeviceToHost);
  // Get the semantic table on CPU and add as unary potentials
  float* prob_table = class_probabilities_gpu_->mutable_cpu_data();

  std::vector<int> valid_ids;
  for (int i = 0; i < current_table_size_; ++i) {
    valid_ids.push_back(i);
  }
  std::vector<float> unary_potentials(valid_ids.size() * num_classes_);
  for(int i = 0; i < static_cast<int>(valid_ids.size()); ++i) {
    int id = valid_ids[i];
    for (int j = 0; j < num_classes_; ++j) {
       unary_potentials[i * num_classes_ + j] = -log(prob_table[j * max_components_ + id] + 1.0e-12);
    }
  }
  DenseCRF3D crf(valid_ids.size(),num_classes_,0.05,20,0.1);
  crf.setUnaryEnergy(unary_potentials.data());
  // Add pairwise energies
  crf.addPairwiseGaussian(my_surfels,3,valid_ids);
  crf.addPairwiseBilateral(my_surfels,10,valid_ids);
  // Finally read the values back to the probability table 
  float* resulting_probs = crf.runInference(iterations, 1.0);
  for (int i = 0; i < static_cast<int>(valid_ids.size()); ++i) {
    for (int j = 0; j < num_classes_; ++j) {
	  const int id = valid_ids[i];
      // Sometimes it returns nan resulting probs... filter these out
      if (resulting_probs[i * num_classes_ + j] > 0.0 && resulting_probs[i * num_classes_ + j] < 1.0) {
        prob_table[j * max_components_ + id] = resulting_probs[i * num_classes_ + j];
      }
    }
  }
  const float* gpu_prob_table = class_probabilities_gpu_->gpu_data();
  float* gpu_max_map = class_max_gpu_->mutable_gpu_data();
  updateMaxClass(current_table_size_,gpu_prob_table,num_classes_,gpu_max_map,max_components_);
  map->UpdateSurfelClassGpu(max_components_,class_max_gpu_->gpu_data(),class_max_gpu_->gpu_data() + max_components_,colour_threshold_);
  delete [] my_surfels;
}

void SemanticFusionInterface::SaveArgMaxPredictions(std::string& filename,const std::unique_ptr<ElasticFusionInterface>& map) {
  const float* max_prob = class_max_gpu_->cpu_data() + max_components_;
  const float* max_class = class_max_gpu_->cpu_data();
  const std::vector<int>& surfel_ids = map->GetSurfelIdsCpu();
  cv::Mat argmax_image(240,320,CV_8UC3);
  for (int h = 0; h < 240; ++h) {
    for (int w = 0; w < 320; ++w) {
      float this_max_prob = 0.0;
      int this_max_class = 0;
      const int start = 0;
      const int end = 2;
      
      // As segmentation mask is 320x240 while the orginal image is 640x480, used the highest probability of the 2x2 patch
      // and corresponding class label for the pixel in the segmentation mask
      for (int x = start; x < end; ++x) {
        for (int y = start; y < end; ++y) {
          int id = surfel_ids[((h * 2) + y) * 640 + (w * 2 + x)];
          if (id > 0 && id < current_table_size_) {
            if (max_prob[id] > this_max_prob) {
              this_max_prob = max_prob[id];
              this_max_class = max_class[id];
            }
          }
        }
      }
      argmax_image.at<cv::Vec3b>(h,w)[0] = static_cast<int>(this_max_class);
      argmax_image.at<cv::Vec3b>(h,w)[1] =  static_cast<int>(this_max_class);
      argmax_image.at<cv::Vec3b>(h,w)[2] =  static_cast<int>(this_max_class);
    }
  }
  cv::imwrite(filename,argmax_image);
}
