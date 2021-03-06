/*
 * This file is part of ElasticFusion.
 *
 * Copyright (C) 2015 Imperial College London
 * 
 * The use of the code within this file and all code within files that 
 * make up the software that is ElasticFusion is permitted for 
 * non-commercial purposes only.  The full terms and conditions that 
 * apply to the code within this file are detailed within the LICENSE.txt 
 * file and at <http://www.imperial.ac.uk/dyson-robotics-lab/downloads/elastic-fusion/elastic-fusion-license/> 
 * unless explicitly stated.  By downloading this file you agree to 
 * comply with these terms.
 *
 * If you wish to use any of this code for commercial purposes then 
 * please email researchcontracts.engineering@imperial.ac.uk.
 *
 */

#ifndef MASKLOGREADER_H_
#define MASKLOGREADER_H_

#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <signal.h>

#include "LogReader.h"
#include <string>
#include <vector>

// extern const int MAX_NUM_OBJECTS = 20;

// struct MaskInfo{
//   int mask_id;
//   int class_id;
//   float probability;
//   int x1, y1, x2, y2;
//   std::string mask_image_path;
//   cv::Mat cv_mat;
// };

// struct FrameInfoMask {
//   int64_t timestamp;
//   std::string depth_path;
//   std::string rgb_path;
//   std::string depth_id;
//   std::string rgb_id;
//   bool labeled_frame;
//   std::string frame_id;
//   std::vector<MaskInfo> masks_;
//   int num_masks;
// };

class MaskLogReader : public LogReader
{
public:
	MaskLogReader(std::string file, std::string labels_file);

	virtual ~MaskLogReader();

  void getNext();

  int getNumFrames();

  int getNumMasks();

  std::vector<MaskInfo> getMasks();

  bool hasMore();

  bool isLabeledFrame();
  
  std::string getLabelFrameId();

  bool rewound() { return false; }

  void getBack() { }

  void fastForward(int frame) {}

  void setAuto(bool value) {}

  const std::string getFile() {
    return file;
  };

  bool hasDepthFilled() { return has_depth_filled; }

  std::vector<MaskInfo> masksinfo;
private:
  int64_t lastFrameTime;
  int lastGot;
  std::vector<FrameInfoMask> frames_;
  Bytef * decompressionBufferDepthFilled;
  bool has_depth_filled;
protected:
  int num_labelled;
};

#endif /* MASKLOGREADER_H_ */
