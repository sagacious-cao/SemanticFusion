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

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <cassert>

#include <cnn_interface/CaffeInterface.h>
#include <map_interface/ElasticFusionInterface.h>
#include <semantic_fusion/SemanticFusionInterface.h>
#include <semantic_fusion/ObjectFusionInterface.h>

#include <utilities/LiveLogReader.h>
#include <utilities/RawLogReader.h>
#include <utilities/PNGLogReader.h>
#include <utilities/MaskLogReader.h>
#include <utilities/Types.h>

#include <gui/Gui.h>

std::vector<ClassColour> load_colour_scheme_(std::string filename, int num_classes) {
  std::vector<ClassColour> colour_scheme(num_classes);
  std::ifstream file(filename);
  std::string str; 
  int line_number = 1;
  while (std::getline(file, str))
  {
    std::istringstream buffer(str);
    std::string textual_prefix;
    int id, r, g, b;
    if (line_number > 2) {
      buffer >> textual_prefix >> id >> r >> g >> b;
      ClassColour class_colour(textual_prefix,r,g,b);
      assert(id < num_classes);
      colour_scheme[id] = class_colour;
    }
    line_number++;
  }
  return colour_scheme;
}

int main_1(int argc, char *argv[])
{
  // CNN Skip params
  const int cnn_skip_frames = 0;
  
  // Option CPU-based CRF smoothing
  const bool use_crf = false;
  const int crf_skip_frames = 500;
  const int crf_iterations = 10;
  
  // // Load the network model and parameters
  // CaffeInterface caffe;
  
  // // This is for the RGB-D network
  // caffe.Init("../caffe_semanticfusion/models/nyu_rgbd/inference.prototxt","../caffe_semanticfusion/models/nyu_rgbd/inference.caffemodel");
  // // This is for the RGB network
  // caffe.Init("../caffe_semanticfusion/models/nyu_rgb/inference.prototxt","../caffe_semanticfusion/models/nyu_rgb/inference.caffemodel");
  
  // const int num_classes = caffe.num_output_classes();
  const int num_classes = 14;
  std::cout<<"Network produces "<<num_classes<<" output classes"<<std::endl;
  // Check the class colour output and the number of classes matches
  std::vector<ClassColour> class_colour_lookup = load_colour_scheme_("../class_colour_scheme.data",num_classes);
  
  // const int num_classes = 81;
  std::cout<<"initialising ObjectFusionInterface" << std::endl;
  std::unique_ptr<ObjectFusionInterface> object_fusion(new ObjectFusionInterface(num_classes,100));
  
  // Initialise the Gui, Map, and Kinect Log Reader
  const int width = 640;
  const int height = 480;
  Resolution::getInstance(width, height);
  Intrinsics::getInstance(528, 528, 320, 240);
  
  std::cout<<"Initialising Gui" << std::endl;
  std::unique_ptr<Gui> gui(new Gui(true,class_colour_lookup,640,480));
  
  std::cout<<"Initialising ElasticFusionInterface" << std::endl;
  std::unique_ptr<ElasticFusionInterface> map(new ElasticFusionInterface());
  
  // Choose the input Reader, live for a running OpenNI device, PNG for textfile lists of PNG frames
  std::cout<<"Initialising LogReader" << std::endl;  
  // std::unique_ptr<LogReader> log_reader;
  // if (argc > 2) {
  //   log_reader.reset(new MaskLogReader(argv[1],argv[2]));
  // } else {
  //   log_reader.reset(new LiveLogReader("./live",false));
  //   if (!log_reader->is_valid()) {
  //     std::cout<<"Error initialising live device..."<<std::endl;
  //     return 1;
  //   }
  // }

  std::unique_ptr<MaskLogReader> log_reader(new MaskLogReader(argv[1],argv[2]));
  
  

  if (!map->Init(class_colour_lookup)) {
    std::cout<<"ElasticFusionInterface init failure"<<std::endl;
  }
  
  // Frame numbers for logs
  int frame_num = 0;
  // std::shared_ptr<caffe::Blob<float> > segmented_prob;
  
  // running loop
  std::cout<< "Start Running!" <<std::endl;

  while(frame_num < 10){
    log_reader->getNext();
    std::vector<MaskInfo> masks = log_reader->getMasks();
    
    // printf("%s\n", "GotMasks");
    // printf("%i\n", log_reader->getNumMasks());
    // cv::Mat mask = masks[0].cv_mat;
    // printf("%s\n", "GotMask");
    // unsigned char *mask_data = mask.data;
    // printf("%s\n", "GotMasksData");

    // for(int j = 0;j < mask.rows;j++){
    //   for(int i = 0;i < mask.cols;i++){
    //       unsigned char prob = mask_data[mask.step * j + i ] ;
    //       std::cout<<int(prob)<<",";
    //   }
    //   std::cout<<std::endl;
    // }

    map->setTrackingOnly(false);

    std::cout << "ProcessFrame" << std::endl;
    if (!map->ProcessFrame(log_reader->rgb, log_reader->depth,log_reader->timestamp)) {
      std::cout<<"Elastic fusion lost!"<<argv[1]<<std::endl;
      return 1;
    }
    std::cout << "UpdateProbabilityTable" << std::endl;
    object_fusion->UpdateObjectTable(map);
    
    if (cnn_skip_frames == 0 || frame_num == 0 || (frame_num > 1 && ((frame_num + 1) % cnn_skip_frames == 0))) {

      //printf("%f\n", segmented_prob->shape());
      //const float* prob_cpu = segmented_prob->cpu_data();
       object_fusion->UpdateObjectIds(&masks,log_reader->getNumMasks(), map);
    }

    frame_num++;
  }
  

  // while(!pangolin::ShouldQuit() && log_reader->hasMore()) {
  //   printf("frame %i\n", frame_num);
  //   //std::cout<< "GUI preCall!" <<std::endl;
  //   gui->preCall();
    
  //   // Read and perform an elasticFusion update
  //   if (!gui->paused() || gui->step()) {
  //     //std::cout << "getNext" << std::endl;
  //     log_reader->getNext();
      
  //     //std::cout << "setTrackingOnly" << std::endl;
  //     map->setTrackingOnly(gui->tracking());
      
  //     //std::cout << "ProcessFrame" << std::endl;
  //     if (!map->ProcessFrame(log_reader->rgb, log_reader->depth,log_reader->timestamp)) {
  //       std::cout<<"Elastic fusion lost!"<<argv[1]<<std::endl;
  //       return 1;
  //     }
      
  //     // This queries the map interface to update the indexes within the table 
  //     // It MUST be done everytime ProcessFrame is performed as long as the map
  //     // is not performing tracking only (i.e. fine to not call, when working
  //     // with a static map)
      
  //     //std::cout << "UpdateProbabilityTable" << std::endl;
  //     if(!gui->tracking()) {
  //       semantic_fusion->UpdateProbabilityTable(map);
  //     }
      
  //     // We do not need to perform a CNN update every frame, we perform it every
  //     // 'cnn_skip_frames'
  //     //std::cout << "Caffe ProcessFrame" << std::endl;

  //     if (frame_num == 0 || (frame_num > 1 && ((frame_num + 1) % cnn_skip_frames == 0))) {
  //       if (log_reader->hasDepthFilled()) {
  //         segmented_prob = caffe.ProcessFrame(log_reader->rgb, log_reader->depthfilled, height, width);
  //       } else {
  //         segmented_prob = caffe.ProcessFrame(log_reader->rgb, log_reader->depth, height, width);
  //       }
  //       //printf("%f\n", segmented_prob->shape());
  //       //const float* prob_cpu = segmented_prob->cpu_data();
  //      	semantic_fusion->UpdateProbabilities(segmented_prob,map);
  //     }
      
  //     //crf update
  //     if (use_crf && frame_num % crf_skip_frames == 0) {
  //       //std::cout<<"Performing CRF Update..."<<`std::endl;
  //       semantic_fusion->CRFUpdate(map,crf_iterations);
  //     } 
  //   }
  //   frame_num++;
    
  //   // This is for outputting the predicted frames
  //   if (log_reader->isLabeledFrame()) {
  //     // Change this to save the NYU raw label predictions to a folder.
  //     // Note these are raw, without the CNN fall-back predictions where there
  //     // is no surfel to give a prediction.
  //     std::string save_dir("./");
  //     std::string label_dir(log_reader->getLabelFrameId());
  //     std::string suffix("_label.png");
  //     save_dir += label_dir;
  //     save_dir += suffix;
  //     std::cout<<"Saving labeled frame to "<<save_dir<<std::endl;
  //     semantic_fusion->SaveArgMaxPredictions(save_dir,map);
  //   }
  //   gui->renderMap(map);
  //   gui->displayRawNetworkPredictions("pred",segmented_prob->mutable_gpu_data());
  //   // This is to display a predicted semantic segmentation from the fused map
  //   semantic_fusion->CalculateProjectedProbabilityMap(map);
  //   gui->displayArgMaxClassColouring("segmentation",semantic_fusion->get_rendered_probability()->mutable_gpu_data(),
  //                                    num_classes,semantic_fusion->get_class_max_gpu()->gpu_data(),
  //                                    semantic_fusion->max_num_components(),map->GetSurfelIdsGpu(),0.0);
  //   // This one requires the size of the segmentation display to be set in the Gui constructor to 224,224
  //   gui->displayImg("raw",map->getRawImageTexture());
  //   gui->postCall();
  //   if (gui->reset()) {
  //     map.reset(new ElasticFusionInterface());
  //     if (!map->Init(class_colour_lookup)) {
  //       std::cout<<"ElasticFusionInterface init failure"<<std::endl;
  //     }
  //   }
  // }
  // std::cout<<"Finished SemanticFusion"<<std::endl;
  return 0;
}
