#include <OpenANN/io/DataStream.h>
#include <OpenANN/io/DirectStorageDataSet.h>
#include <OpenANN/optimization/Optimizer.h>
#include <OpenANN/optimization/StoppingCriteria.h>
#include <OpenANN/util/AssertionMacros.h>
#include <OpenANN/Learner.h>
#include <OpenANN/io/Logger.h>

namespace OpenANN
{

DataStream::DataStream(int cacheSize)
  : cacheSize(cacheSize), collected(0), cache(0), opt(0), learner(0)
{
}

DataStream::~DataStream()
{
  if(cache)
    delete cache;
}

DataStream& DataStream::setLearner(Learner& learner)
{
  this->learner = &learner;
  return *this;
}

DataStream& DataStream::setOptimizer(Optimizer& opt)
{
  this->opt = &opt;
  StoppingCriteria stop;
  opt.setStopCriteria(stop);
  return *this;
}

void DataStream::addSample(Eigen::VectorXd* x, Eigen::VectorXd* t)
{
  OPENANN_CHECK(learner);
  OPENANN_CHECK(opt);

  if(!cache)
    initialize(x->rows(), t ? t->rows() : 0);

  X.row(collected) = *x;
  if(t) T.row(collected) = *t;
  if(++collected >= cacheSize)
  {
    opt->step();
    collected = 0;
  }
}

void DataStream::initialize(int inputs, int outputs)
{
  OPENANN_CHECK(learner);
  OPENANN_CHECK(opt);
  X.conservativeResize(cacheSize, inputs);
  if(outputs > 0)
  {
    T.conservativeResize(cacheSize, outputs);
    cache = new DirectStorageDataSet(&X, &T);
  }
  else
  {
    cache = new DirectStorageDataSet(&X);
  }
  learner->trainingSet(*cache);
  opt->setOptimizable(*learner);
  OPENANN_DEBUG << "Initialized DataStream.";
}

}
