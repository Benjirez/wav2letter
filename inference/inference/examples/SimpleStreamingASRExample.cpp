/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

/**
 * User guide
 * ----------
 *
 * 1. Setup the input files:
 * Assuming that you have the acoustic model, language model, features
 * extraction serialized streaming inference DNN, tokens file, lexicon file and
 * input audio file in a directory called modules. > ls ~/modules am.bin
 * language.bin
 * feat.bin
 * tokens.txt
 * lexicon.txt
 * audio.wav
 *
 * 2. Run:
 * simple_wav2letter_example -input_files_base_path=~/modules
 *
 * Input files expect by default the naming convention we show above
 * in the example ~/modules directory. Input files can be specified by flags.
 * input files can be specified as a full path or as a file name prefixed by
 * the --input_files_base_path flag. In order to process the input audio file
 * from standard input rather than a file, set --input_audio_file=""
 *
 * cat audio.wav | simple_wav2letter_example --input_files_base_path=~/modules
 *                                           --feature_module_file=features_123.bin
 *                                           --acoustic_module_file=/tmp/am.bin
 *                                           --input_audio_file=""
 *
 * Example output:
 * Feat file loaded
 * AM file loaded
 * Tokens loaded - 9998
 * [Letters] 9998 tokens loaded.
 * [Words] 200001 words loaded.
 * WordLMDecoder is in use.
 * Reading input wav file from stdin...
 * start: 0 ms - end: 500 ms :
 * start: 500 ms - end: 1000 ms :
 * start: 1000 ms - end: 1500 ms : uncle
 * start: 1500 ms - end: 2000 ms : julia said
 * start: 2000 ms - end: 2500 ms :
 * start: 2500 ms - end: 3000 ms : and auntie
 * start: 3000 ms - end: 3500 ms : helen
 *
 */

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <gflags/gflags.h>

#include "inference/decoder/Decoder.h"
#include "inference/examples/AudioToWords.h"
#include "inference/examples/Util.h"
#include "inference/module/feature/feature.h"
#include "inference/module/module.h"
#include "inference/module/nn/nn.h"

using namespace w2l;
using namespace w2l::streaming;

DEFINE_string(
    input_files_base_path,
    ".",
    "path is added as prefix to input files unless the input file"
    " is a full path.");
DEFINE_string(
    feature_module_file,
    "feature_extractor.bin",
    "serialized feature extraction module.");
DEFINE_string(
    acoustic_module_file,
    "acoustic_model.bin",
    "binary file containing acoustic module parameters.");
DEFINE_string(tokens_file, "tokens.txt", "text file containing tokens.");
DEFINE_string(lexicon_file, "lexicon.txt", "text file containing lexicon.");
DEFINE_string(
    input_audio_file,
    "",
    "16KHz wav audio input file to be traslated to words. "
    "If no file is specified then it is read of standard input.");
DEFINE_string(silence_token, "_", "the token to use to denote silence");
DEFINE_string(
    language_model_file,
    "language_model.bin",
    "binary file containing language module parameters.");
DEFINE_string(
    decoder_options_file,
    "decoder_options.json",
    "JSON file containing decoder options"
    " including: max overall beam size, max beam for token selection, beam score threshold"
    ", language model weight, word insertion score, unknown word insertion score"
    ", silence insertion score, and use logadd when merging decoder nodes");

std::string GetInputFileFullPath(const std::string& fileName) {
  return GetFullPath(fileName, FLAGS_input_files_base_path);
}

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::shared_ptr<streaming::Sequential> featureModule;
  std::shared_ptr<streaming::Sequential> acousticModule;

  // Read files
  {
    TimeElapsedReporter feturesLoadingElapsed("features model file loading");
    std::ifstream featFile(
        GetInputFileFullPath(FLAGS_feature_module_file), std::ios::binary);
    if (!featFile.is_open()) {
      throw std::runtime_error(
          "failed to open feature file=" +
          GetInputFileFullPath(FLAGS_feature_module_file) + " for reading");
    }
    cereal::BinaryInputArchive ar(featFile);
    ar(featureModule);
  }

  {
    TimeElapsedReporter acousticLoadingElapsed("acoustic model file loading");
    std::ifstream amFile(
        GetInputFileFullPath(FLAGS_acoustic_module_file), std::ios::binary);
    if (!amFile.is_open()) {
      throw std::runtime_error(
          "failed to open acoustic model file=" +
          GetInputFileFullPath(FLAGS_feature_module_file) + " for reading");
    }
    cereal::BinaryInputArchive ar(amFile);
    ar(acousticModule);
  }

  // String both modeles togthers to a single DNN.
  auto dnnModule = std::make_shared<streaming::Sequential>();
  dnnModule->add(featureModule);
  dnnModule->add(acousticModule);

  std::vector<std::string> tokens;
  {
    TimeElapsedReporter acousticLoadingElapsed("tokens file loading");
    std::ifstream tknFile(GetInputFileFullPath(FLAGS_tokens_file));
    if (!tknFile.is_open()) {
      throw std::runtime_error(
          "failed to open tokens file=" +
          GetInputFileFullPath(FLAGS_tokens_file) + " for reading");
    }
    std::string line;
    while (std::getline(tknFile, line)) {
      tokens.push_back(line);
    }
  }
  int nTokens = tokens.size();
  std::cout << "Tokens loaded - " << nTokens << " tokens" << std::endl;

  DecoderOptions decoderOptions;
  {
    TimeElapsedReporter decoderOptionsElapsed("decoder options file loading");
    std::ifstream decoderOptionsFile(
        GetInputFileFullPath(FLAGS_decoder_options_file));
    if (!decoderOptionsFile.is_open()) {
      throw std::runtime_error(
          "failed to open decoder options file=" +
          GetInputFileFullPath(FLAGS_decoder_options_file) + " for reading");
    }
    cereal::JSONInputArchive ar(decoderOptionsFile);
    // TODO: factor out proper serialization functionality or Cereal
    // specialization.
    ar(cereal::make_nvp("beamSize", decoderOptions.beamSize),
       cereal::make_nvp("beamSizeToken", decoderOptions.beamSizeToken),
       cereal::make_nvp("beamThreshold", decoderOptions.beamThreshold),
       cereal::make_nvp("lmWeight", decoderOptions.lmWeight),
       cereal::make_nvp("wordScore", decoderOptions.wordScore),
       cereal::make_nvp("unkScore", decoderOptions.unkScore),
       cereal::make_nvp("silScore", decoderOptions.silScore),
       cereal::make_nvp("eosScore", decoderOptions.eosScore),
       cereal::make_nvp("logAdd", decoderOptions.logAdd),
       cereal::make_nvp("criterionType", decoderOptions.criterionType));
  }

  std::shared_ptr<const DecoderFactory> decoderFactory;
  // Create Decoder
  {
    TimeElapsedReporter acousticLoadingElapsed("create decoder");
    std::vector<float> transitions; // unused for now
    decoderFactory = std::make_shared<DecoderFactory>(
        GetInputFileFullPath(FLAGS_tokens_file),
        GetInputFileFullPath(FLAGS_lexicon_file),
        GetInputFileFullPath(FLAGS_language_model_file),
        transitions,
        SmearingMode::MAX,
        FLAGS_silence_token,
        0);
  }

  if (FLAGS_input_audio_file.empty()) {
    TimeElapsedReporter feturesLoadingElapsed(
        "converting audio input from stdin to text...");
    audioStreamToWordsStream(
        std::cin,
        std::cout,
        dnnModule,
        decoderFactory,
        decoderOptions,
        nTokens);
  } else {
    const std::string input_audio_file =
        GetInputFileFullPath(FLAGS_input_audio_file);
    std::ifstream audioFile(input_audio_file, std::ios::binary);
    TimeElapsedReporter feturesLoadingElapsed(
        "converting audio input file=" + input_audio_file + " to text...");

    audioStreamToWordsStream(
        audioFile,
        std::cout,
        dnnModule,
        decoderFactory,
        decoderOptions,
        nTokens);
  }

  return 0;
}
