#pragma once

#include <AssertionMacros.h>
#include <Eigen/Dense>
#include <fstream>
#include <io/Logger.h>
#include <stdint.h>
#include <endian.h>
#include <CompressionMatrixFactory.h>
#include "Distorter.h"

class IDXLoader
{
public:
  int padToX;
  int padToY;
  int trainingN;
  int testN;
  int D;
  int F;
  Mt trainingInput, trainingOutput, testInput, testOutput;
  OpenANN::Logger debugLogger;

  IDXLoader(int padToX = 29, int padToY = 29, int loadTraininN = -1, int loadTestN = -1)
    : padToX(padToX), padToY(padToY), trainingN(0), testN(0), D(0), F(0), debugLogger(OpenANN::Logger::CONSOLE)
  {
    load(true, loadTraininN);
    load(false, loadTestN);
    debugLogger << "Loaded MNIST data set.\n"
        << "trainingN = " << trainingN << "\n"
        << "testN = " << testN << "\n"
        << "D = " << D << ", F = " << F << "\n";
  }

  void load(bool train, int maxN)
  {
    int& N = train ? trainingN : testN;
    Mt& input = train ? trainingInput : testInput;
    Mt& output = train ? trainingOutput : testOutput;
    unsigned char tmp = 0;

    std::fstream inputFile(train ? "mnist/train-images-idx3-ubyte"
        : "mnist/t10k-images-idx3-ubyte", std::ios::in | std::ios::binary);
    OPENANN_CHECK(inputFile.is_open());
    int8_t zero = 0, encoding = 0, dimension = 0;
    int32_t images = 0, rows = 0, cols = 0, items = 0;
    inputFile.read(reinterpret_cast<char*>(&zero), sizeof(zero));
    OPENANN_CHECK_EQUALS(0, (int) zero);
    inputFile.read(reinterpret_cast<char*>(&zero), sizeof(zero));
    OPENANN_CHECK_EQUALS(0, (int) zero);
    inputFile.read(reinterpret_cast<char*>(&encoding), sizeof(encoding));
    OPENANN_CHECK_EQUALS(8, (int) encoding);
    inputFile.read(reinterpret_cast<char*>(&dimension), sizeof(dimension));
    OPENANN_CHECK_EQUALS(3, (int) dimension);
    read(inputFile, images);
    read(inputFile, cols);
    read(inputFile, rows);
    D = (int) (rows * cols);
    N = (int) images;
    if(maxN > 0)
      N = maxN;
    if(D < padToX * padToY)
      D = padToX * padToY;
    int colNumber = padToX > (int)cols ? padToX : (int)cols;

    input.resize(D, N);
    for(int n = 0; n < N; n++)
    {
      int r = 0;
      for(; r < (int) rows; r++)
      {
        int c = 0;
        for(; c < (int) cols; c++)
        {
          read(inputFile, tmp);
          double value = (double) tmp;
          input(r*colNumber+c, n) = 1.0-value/255.0; // scale to [0:1]
        }
        int lastC = c-1;
        for(; c < padToX; c++)
        {
          input(r*colNumber+c, n) = input(r*colNumber+lastC, n);
        }
      }
      int lastR = r-1;
      for(; r < padToY; r++)
      {
        for(int c = 0; c < padToX; c++)
        {
          input(r*colNumber+c, n) = input(lastR*colNumber+c, n);
        }
      }
    }

    std::fstream labelFile(train ? "mnist/train-labels-idx1-ubyte"
        : "mnist/t10k-labels-idx1-ubyte", std::ios::in | std::ios::binary);
    OPENANN_CHECK(labelFile.is_open());
    labelFile.read(reinterpret_cast<char*>(&zero), sizeof(zero));
    OPENANN_CHECK_EQUALS(0, (int) zero);
    labelFile.read(reinterpret_cast<char*>(&zero), sizeof(zero));
    OPENANN_CHECK_EQUALS(0, (int) zero);
    labelFile.read(reinterpret_cast<char*>(&encoding), sizeof(encoding));
    OPENANN_CHECK_EQUALS(8, (int) encoding);
    labelFile.read(reinterpret_cast<char*>(&dimension), sizeof(dimension));
    OPENANN_CHECK_EQUALS(1, (int) dimension);
    read(labelFile, items);
    OPENANN_CHECK_EQUALS(images, items);
    F = 10;

    output.resize(F, N);
    for(int n = 0; n < N; n++)
    {
      read(labelFile, tmp);
      for(int c = 0; c < F; c++)
        output(c, n) = (255 - (int) tmp) == c ? 1.0 : 0.0;
    }
  }

  void compress(int paramDim, OpenANN::CompressionMatrixFactory::Transformation transformation)
  {
    OpenANN::CompressionMatrixFactory cmf(D, paramDim, transformation);
    Mt compressionMatrix;
    cmf.createCompressionMatrix(compressionMatrix);
    {
      Mt compressedInput = compressionMatrix * trainingInput;
      trainingInput.resize(compressedInput.rows(), compressedInput.cols());
      trainingInput = compressedInput;
    }
    {
      Mt compressedInput = compressionMatrix * testInput;
      testInput.resize(compressedInput.rows(), compressedInput.cols());
      testInput = compressedInput;
    }
    D = paramDim;
  }

  void distort(int multiplier = 10)
  {
    debugLogger << "Start creating " << (multiplier-1) << " distortions.\n";
    int originalN = trainingN;
    trainingN *= multiplier;
    Mt originalInput = trainingInput;
    trainingInput.resize(D, trainingN);
    trainingInput.block(0, 0, D, originalN) = originalInput;
    Mt originalOutput = trainingOutput;
    trainingOutput.resize(F, trainingN);
    trainingOutput.block(0, 0, F, originalN) = originalOutput;

    Vt instance(D);
    for(int distortion = 1; distortion < multiplier; distortion++)
    {
      Distorter distorter;
      distorter.createDistortionMap(padToX, padToY);
      for(int n = distortion*originalN; n < (distortion+1)*originalN; n++)
      {
        instance = originalInput.col(n % originalN);
        distorter.applyDistortion(instance);
        trainingInput.col(n) = instance;
      }
      trainingOutput.block(0, distortion*originalN, F, originalN) = originalOutput;
      debugLogger << "Finished distortion " << distortion << ".\n";
    }
  }

private:
  template<typename T>
  void read(std::fstream& stream, T& t)
  {
    stream.read(reinterpret_cast<char*>(&t), sizeof(t));
    if(sizeof(t) == 4)
      t = htobe32(t);
    else if(sizeof(t) == 1)
      t = 255 - t;
  }
};