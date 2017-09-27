
/*
    pbrt source code is Copyright(c) 1998-2016
                        Matt Pharr, Greg Humphreys, and Wenzel Jakob.

    This file is part of pbrt.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */


// integrators/mlt.cpp*
#include "integrators/mlt.h"
#include "integrators/bdpt.h"
#include "scene.h"
#include "film.h"
#include "image.h"
#include "sampler.h"
#include "lightdistrib.h"
#include "integrator.h"
#include "camera.h"
#include "util/stats.h"
#include "filters/box.h"
#include "paramset.h"
#include "sampling.h"
#include "util/progressreporter.h"

namespace pbrt {

STAT_PERCENT("Integrator/Acceptance rate", acceptedMutations, totalMutations);

// MLTSampler Constants
static const int cameraStreamIndex = 0;
static const int lightStreamIndex = 1;
static const int connectionStreamIndex = 2;
static const int nSampleStreams = 3;

// MLTSampler Method Definitions
Float MLTSampler::Get1D() {
    ProfilePhase _(Prof::GetSample);
    int index = GetNextIndex();
    EnsureReady(index);
    return X[index].value;
}

Point2f MLTSampler::Get2D() { return {Get1D(), Get1D()}; }

std::unique_ptr<Sampler> MLTSampler::Clone() {
    LOG(FATAL) << "MLTSampler::Clone() is not implemented";
    return nullptr;
}

void MLTSampler::Request1DArray(int n) {
    LOG(FATAL) << "Unimplemented";
}

void MLTSampler::Request2DArray(int n) {
    LOG(FATAL) << "Unimplemented";
}

absl::Span<const Float> MLTSampler::Get1DArray(int n) {
    LOG(FATAL) << "Unimplemented";
    return {};
}

absl::Span<const Point2f> MLTSampler::Get2DArray(int n) {
    LOG(FATAL) << "Unimplemented";
    return {};
}

void MLTSampler::StartIteration() {
    currentIteration++;
    largeStep = rng.UniformFloat() < largeStepProbability;
}

void MLTSampler::Accept() {
    if (largeStep) lastLargeStepIteration = currentIteration;
}

void MLTSampler::EnsureReady(int index) {
    // Enlarge _MLTSampler::X_ if necessary and get current $\VEC{X}_i$
    if (index >= X.size()) X.resize(index + 1);
    PrimarySample &Xi = X[index];

    // Reset $\VEC{X}_i$ if a large step took place in the meantime
    if (Xi.lastModificationIteration < lastLargeStepIteration) {
        Xi.value = rng.UniformFloat();
        Xi.lastModificationIteration = lastLargeStepIteration;
    }

    // Apply remaining sequence of mutations to _sample_
    Xi.Backup();
    if (largeStep) {
        Xi.value = rng.UniformFloat();
    } else {
        int64_t nSmall = currentIteration - Xi.lastModificationIteration;
        // Apply _nSmall_ small step mutations

        // Sample the standard normal distribution $N(0, 1)$
        Float normalSample = Sqrt2 * ErfInv(2 * rng.UniformFloat() - 1);

        // Compute the effective standard deviation and apply perturbation to
        // $\VEC{X}_i$
        Float effSigma = sigma * std::sqrt((Float)nSmall);
        Xi.value += normalSample * effSigma;
        Xi.value -= std::floor(Xi.value);
    }
    Xi.lastModificationIteration = currentIteration;
}

void MLTSampler::Reject() {
    for (auto &Xi : X)
        if (Xi.lastModificationIteration == currentIteration) Xi.Restore();
    --currentIteration;
}

void MLTSampler::StartStream(int index) {
    CHECK_LT(index, streamCount);
    streamIndex = index;
    sampleIndex = 0;
}

// MLT Method Definitions
Spectrum MLTIntegrator::L(const Scene &scene, MemoryArena &arena,
                          MLTSampler &sampler, int depth, Point2f *pRaster) {
    sampler.StartStream(cameraStreamIndex);
    // Determine the number of available strategies and pick a specific one
    int s, t, nStrategies;
    if (depth == 0) {
        nStrategies = 1;
        s = 0;
        t = 2;
    } else {
        nStrategies = depth + 2;
        s = sampler.GetDiscrete1D(nStrategies);
        t = nStrategies - s;
    }

    // Generate a camera subpath with exactly _t_ vertices
    Vertex *cameraVertices = arena.AllocArray<Vertex>(t);
    Bounds2f sampleBounds = (Bounds2f)camera->film->GetSampleBounds();
    *pRaster = sampleBounds.Lerp(sampler.Get2D());
    if (GenerateCameraSubpath(scene, sampler, arena, t, *camera, *pRaster,
                              cameraVertices) != t)
        return Spectrum(0.f);

    // Generate a light subpath with exactly _s_ vertices
    sampler.StartStream(lightStreamIndex);
    Vertex *lightVertices = arena.AllocArray<Vertex>(s);
    if (GenerateLightSubpath(scene, sampler, arena, s, cameraVertices[0].time(),
                             *lightDistr, lightVertices) != s)
        return Spectrum(0.f);

    // Execute connection strategy and return the radiance estimate
    sampler.StartStream(connectionStreamIndex);
    return ConnectBDPT(scene, lightVertices, cameraVertices, s, t, *lightDistr,
                       *camera, sampler, pRaster) *
           nStrategies;
}

void MLTIntegrator::Render(const Scene &scene) {
    lightDistr = std::make_unique<PowerLightDistribution>(scene);

    // Generate bootstrap samples and compute normalization constant $b$
    int nBootstrapSamples = nBootstrap * (maxDepth + 1);
    std::vector<Float> bootstrapWeights(nBootstrapSamples, 0);
    if (scene.lights.size() > 0) {
        ProgressReporter progress(nBootstrap / 256,
                                  "Generating bootstrap paths");
        std::vector<MemoryArena> bootstrapThreadArenas(MaxThreadIndex());
        int chunkSize = Clamp(nBootstrap / 128, 1, 8192);
        ParallelFor(0, nBootstrap, chunkSize, [&](int64_t start, int64_t end) {
            MemoryArena &arena = bootstrapThreadArenas[ThreadIndex];
            for (int64_t i = start; i < end; ++i) {
                // Generate _i_th bootstrap sample
                for (int depth = 0; depth <= maxDepth; ++depth) {
                    int rngIndex = i * (maxDepth + 1) + depth;
                    MLTSampler sampler(mutationsPerPixel, rngIndex, sigma,
                                       largeStepProbability, nSampleStreams);
                    Point2f pRaster;
                    bootstrapWeights[rngIndex] =
                        L(scene, arena, sampler, depth, &pRaster).y();
                    arena.Reset();
                }
                if ((i + 1 % 256) == 0) progress.Update();
            }
        });
        progress.Done();
    }
    Distribution1D bootstrap(bootstrapWeights);
    Float b = bootstrap.funcInt * (maxDepth + 1);

    // Run _nChains_ Markov chains in parallel
    Film &film = *camera->film;
    int64_t nTotalMutations =
        (int64_t)mutationsPerPixel * (int64_t)film.GetSampleBounds().Area();
    const int progressFrequency = 32768;
    ProgressReporter progress(nTotalMutations / progressFrequency,
                              "Rendering");
    if (scene.lights.size() > 0) {
        ParallelFor(0, nChains, [&](int i) {
            int64_t nChainMutations =
                std::min((i + 1) * nTotalMutations / nChains, nTotalMutations) -
                i * nTotalMutations / nChains;
            // Follow {i}th Markov chain for _nChainMutations_
            MemoryArena arena;

            // Select initial state from the set of bootstrap samples
            RNG rng(i);
            int bootstrapIndex = bootstrap.SampleDiscrete(rng.UniformFloat());
            int depth = bootstrapIndex % (maxDepth + 1);

            // Initialize local variables for selected state
            MLTSampler sampler(mutationsPerPixel, bootstrapIndex, sigma,
                               largeStepProbability, nSampleStreams);
            Point2f pCurrent;
            Spectrum LCurrent =
                L(scene, arena, sampler, depth, &pCurrent);

            // Run the Markov chain for _nChainMutations_ steps
            for (int64_t j = 0; j < nChainMutations; ++j) {
                sampler.StartIteration();
                Point2f pProposed;
                Spectrum LProposed =
                    L(scene, arena, sampler, depth, &pProposed);
                // Compute acceptance probability for proposed sample
                Float accept = std::min((Float)1, LProposed.y() / LCurrent.y());

                // Splat both current and proposed samples to _film_
                if (accept > 0)
                    film.AddSplat(pProposed,
                                  LProposed * accept / LProposed.y());
                film.AddSplat(pCurrent, LCurrent * (1 - accept) / LCurrent.y());

                // Accept or reject the proposal
                if (rng.UniformFloat() < accept) {
                    pCurrent = pProposed;
                    LCurrent = LProposed;
                    sampler.Accept();
                    ++acceptedMutations;
                } else
                    sampler.Reject();
                ++totalMutations;
                if ((i * nTotalMutations / nChains + j) % progressFrequency ==
                    0)
                    progress.Update();
                arena.Reset();
            }
        });
    }
    progress.Done();

    // Store final image computed with MLT
    ImageMetadata metadata;
    metadata.renderTimeSeconds = progress.ElapsedMS() / 1000.;
    camera->InitMetadata(&metadata);
    camera->film->WriteImage(&metadata, b / mutationsPerPixel);
}

std::unique_ptr<MLTIntegrator> CreateMLTIntegrator(
    const ParamSet &params, std::shared_ptr<const Camera> camera) {
    int maxDepth = params.GetOneInt("maxdepth", 5);
    int nBootstrap = params.GetOneInt("bootstrapsamples", 100000);
    int64_t nChains = params.GetOneInt("chains", 1000);
    int mutationsPerPixel = params.GetOneInt("mutationsperpixel", 100);
    Float largeStepProbability =
        params.GetOneFloat("largestepprobability", 0.3f);
    Float sigma = params.GetOneFloat("sigma", .01f);
    if (PbrtOptions.quickRender) {
        mutationsPerPixel = std::max(1, mutationsPerPixel / 16);
        nBootstrap = std::max(1, nBootstrap / 16);
    }
    return std::make_unique<MLTIntegrator>(camera, maxDepth, nBootstrap,
                                           nChains, mutationsPerPixel, sigma,
                                           largeStepProbability);
}

}  // namespace pbrt
