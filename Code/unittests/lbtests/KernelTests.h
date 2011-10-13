#ifndef HEMELB_UNITTESTS_LBTESTS_KERNELTESTS_H
#define HEMELB_UNITTESTS_LBTESTS_KERNELTESTS_H

#include <cppunit/TestFixture.h>
#include <cstring>
#include <sstream>

#include "lb/kernels/Kernels.h"
#include "lb/kernels/rheologyModels/RheologyModels.h"
#include "unittests/lbtests/LbTestsHelper.h"
#include "unittests/FourCubeLatticeData.h"

namespace hemelb
{
  namespace unittests
  {
    namespace lbtests
    {

      /**
       * Class containing tests for the functionality of the lattice-Boltzmann kernels. This
       * includes the functions for calculating hydrodynamic variables and for performing
       * collisions.
       */
      class KernelTests : public CppUnit::TestFixture
      {
        public:
          void setUp()
          {
            // Initialise the LBM parameters.
            lb::kernels::InitParams initParams;

            latDat = new FourCubeLatticeData();
            initParams.latDat = latDat;
            initParams.siteCount = initParams.latDat->GetLocalFluidSiteCount();
            distribn_t voxelSize = initParams.latDat->GetVoxelSize();
            unsigned timeStepsPerCycle = 1000;
            lbmParams = new lb::LbmParameters(PULSATILE_PERIOD_s / (distribn_t) timeStepsPerCycle,
                                              voxelSize);
            initParams.lbmParams = lbmParams;

            entropic = new lb::kernels::Entropic(initParams);
            lbgk = new lb::kernels::LBGK(initParams);

            /*
             *  We need two kernel instances if we want to work with two different sets of data (and keep the computed
             *  values of tau consistent). One to be used with CalculateDensityVelocityFeq and another with CalculateFeq.
             */
            lbgknn0 = new lb::kernels::LBGKNN<lb::kernels::rheologyModels::CarreauYasudaRheologyModel>(initParams);
            lbgknn1 = new lb::kernels::LBGKNN<lb::kernels::rheologyModels::CarreauYasudaRheologyModel>(initParams);

            numSites = initParams.latDat->GetLocalFluidSiteCount();
          }

          void tearDown()
          {
            delete entropic;
            delete lbgk;
            delete lbgknn0;
            delete lbgknn1;
            delete lbmParams;
            delete latDat;
          }

          void TestEntropicCalculationsAndCollision()
          {
            // Initialise the original f distribution to something asymmetric.
            distribn_t f_original[D3Q15::NUMVECTORS];

            for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
            {
              f_original[ii] = ((float) (1 + ii)) / 10.0;
            }

            /*
             * Case 0: use the function that calculates density, velocity and
             * f_eq.
             * Case 1: use the function that leaves density and velocity and
             * calculates f_eq.
             */
            lb::kernels::HydroVars<lb::kernels::Entropic> hydroVars0(f_original);
            lb::kernels::HydroVars<lb::kernels::Entropic> hydroVars1(f_original);

            // Calculate density, velocity, equilibrium f.
            entropic->CalculateDensityVelocityFeq(hydroVars0, 0);

            // Manually set density and velocity and calculate eqm f.
            hydroVars1.density = 1.0;
            hydroVars1.v_x = 0.4;
            hydroVars1.v_y = 0.5;
            hydroVars1.v_z = 0.6;

            entropic->CalculateFeq(hydroVars1, 1);

            // Calculate expected values in both cases.
            distribn_t expectedDensity0 = 12.0; // (sum 1 to 15) / 10
            distribn_t expectedDensity1 = 1.0; // Should be unchanged

            distribn_t expectedVelocity0[3];
            LbTestsHelper::CalculateVelocity<D3Q15>(hydroVars0.f, expectedVelocity0);
            distribn_t expectedVelocity1[3] = { 0.4, 0.5, 0.6 };

            distribn_t expectedFEq0[D3Q15::NUMVECTORS];
            LbTestsHelper::CalculateEntropicEqmF<D3Q15>(expectedDensity0,
                                                        expectedVelocity0[0],
                                                        expectedVelocity0[1],
                                                        expectedVelocity0[2],
                                                        expectedFEq0);
            distribn_t expectedFEq1[D3Q15::NUMVECTORS];
            LbTestsHelper::CalculateEntropicEqmF<D3Q15>(expectedDensity1,
                                                        expectedVelocity1[0],
                                                        expectedVelocity1[1],
                                                        expectedVelocity1[2],
                                                        expectedFEq1);

            // Now compare the expected and actual values in both cases.
            distribn_t allowedError = 1e-10;

            LbTestsHelper::CompareHydros(expectedDensity0,
                                         expectedVelocity0[0],
                                         expectedVelocity0[1],
                                         expectedVelocity0[2],
                                         expectedFEq0,
                                         "Entropic, case 0",
                                         hydroVars0,
                                         allowedError);
            LbTestsHelper::CompareHydros(expectedDensity1,
                                         expectedVelocity1[0],
                                         expectedVelocity1[1],
                                         expectedVelocity1[2],
                                         expectedFEq1,
                                         "Entropic, case 1",
                                         hydroVars1,
                                         allowedError);

            // Do the collision and test the result.
            distribn_t postCollision0[D3Q15::NUMVECTORS];
            distribn_t postCollision1[D3Q15::NUMVECTORS];

            // Set the values in f_neq.
            for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
            {
              hydroVars0.f_neq[ii] = f_original[ii] - hydroVars0.f_eq[ii];
              hydroVars1.f_neq[ii] = f_original[ii] - hydroVars1.f_eq[ii];
            }

            for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
            {
              postCollision0[ii] = entropic->DoCollide(lbmParams, hydroVars0, ii);
              postCollision1[ii] = entropic->DoCollide(lbmParams, hydroVars1, ii);
            }

            // Get the expected post-collision densities.
            distribn_t expectedPostCollision0[D3Q15::NUMVECTORS];
            distribn_t expectedPostCollision1[D3Q15::NUMVECTORS];

            LbTestsHelper::CalculateEntropicCollision<D3Q15>(f_original,
                                                             hydroVars0.f_eq,
                                                             lbmParams->GetTau(),
                                                             lbmParams->GetBeta(),
                                                             expectedPostCollision0);

            LbTestsHelper::CalculateEntropicCollision<D3Q15>(f_original,
                                                             hydroVars1.f_eq,
                                                             lbmParams->GetTau(),
                                                             lbmParams->GetBeta(),
                                                             expectedPostCollision1);

            // Compare.
            for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
            {
              std::stringstream message("Post-collision ");
              message << ii;

              CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(message.str(),
                                                   postCollision0[ii],
                                                   expectedPostCollision0[ii],
                                                   allowedError);

              CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(message.str(),
                                                   postCollision1[ii],
                                                   expectedPostCollision1[ii],
                                                   allowedError);
            }
          }

          void TestLBGKCalculationsAndCollision()
          {
            // Initialise the original f distribution to something asymmetric.
            distribn_t f_original[D3Q15::NUMVECTORS];

            for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
            {
              f_original[ii] = ((float) (1 + ii)) / 10.0;
            }

            /*
             * Case 0: test the kernel function for calculating density, velocity
             * and f_eq.
             * Case 1: test the function that uses a given density and velocity, and
             * calculates f_eq.
             */
            lb::kernels::HydroVars<lb::kernels::LBGK> hydroVars0(f_original);
            lb::kernels::HydroVars<lb::kernels::LBGK> hydroVars1(f_original);

            // Calculate density, velocity, equilibrium f.
            lbgk->CalculateDensityVelocityFeq(hydroVars0, 0);

            // Manually set density and velocity and calculate eqm f.
            hydroVars1.density = 1.0;
            hydroVars1.v_x = 0.4;
            hydroVars1.v_y = 0.5;
            hydroVars1.v_z = 0.6;

            lbgk->CalculateFeq(hydroVars1, 1);

            // Calculate expected values.
            distribn_t expectedDensity0 = 12.0; // (sum 1 to 15) / 10
            distribn_t expectedDensity1 = 1.0; // Unchanged

            distribn_t expectedVelocity0[3];
            LbTestsHelper::CalculateVelocity<D3Q15>(hydroVars0.f, expectedVelocity0);
            distribn_t expectedVelocity1[3] = { 0.4, 0.5, 0.6 };

            distribn_t expectedFEq0[D3Q15::NUMVECTORS];
            LbTestsHelper::CalculateLBGKEqmF<D3Q15>(expectedDensity0,
                                                    expectedVelocity0[0],
                                                    expectedVelocity0[1],
                                                    expectedVelocity0[2],
                                                    expectedFEq0);
            distribn_t expectedFEq1[D3Q15::NUMVECTORS];
            LbTestsHelper::CalculateLBGKEqmF<D3Q15>(expectedDensity1,
                                                    expectedVelocity1[0],
                                                    expectedVelocity1[1],
                                                    expectedVelocity1[2],
                                                    expectedFEq1);

            // Now compare the expected and actual values.
            distribn_t allowedError = 1e-10;

            LbTestsHelper::CompareHydros(expectedDensity0,
                                         expectedVelocity0[0],
                                         expectedVelocity0[1],
                                         expectedVelocity0[2],
                                         expectedFEq0,
                                         "LBGK, case 0",
                                         hydroVars0,
                                         allowedError);
            LbTestsHelper::CompareHydros(expectedDensity1,
                                         expectedVelocity1[0],
                                         expectedVelocity1[1],
                                         expectedVelocity1[2],
                                         expectedFEq1,
                                         "LBGK, case 1",
                                         hydroVars1,
                                         allowedError);

            // Do the collision and test the result.
            distribn_t postCollision0[D3Q15::NUMVECTORS];
            distribn_t postCollision1[D3Q15::NUMVECTORS];

            // Set the values in f_neq.
            for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
            {
              hydroVars0.f_neq[ii] = f_original[ii] - hydroVars0.f_eq[ii];
              hydroVars1.f_neq[ii] = f_original[ii] - hydroVars1.f_eq[ii];
            }

            for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
            {
              postCollision0[ii] = lbgk->DoCollide(lbmParams, hydroVars0, ii);
              postCollision1[ii] = lbgk->DoCollide(lbmParams, hydroVars1, ii);
            }

            // Get the expected post-collision densities.
            distribn_t expectedPostCollision0[D3Q15::NUMVECTORS];
            distribn_t expectedPostCollision1[D3Q15::NUMVECTORS];

            LbTestsHelper::CalculateLBGKCollision<D3Q15>(f_original,
                                                         hydroVars0.f_eq,
                                                         lbmParams->GetOmega(),
                                                         expectedPostCollision0);

            LbTestsHelper::CalculateLBGKCollision<D3Q15>(f_original,
                                                         hydroVars1.f_eq,
                                                         lbmParams->GetOmega(),
                                                         expectedPostCollision1);

            // Compare.
            for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
            {
              std::stringstream message("Post-collision ");
              message << ii;

              CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(message.str(),
                                                   postCollision0[ii],
                                                   expectedPostCollision0[ii],
                                                   allowedError);

              CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(message.str(),
                                                   postCollision1[ii],
                                                   expectedPostCollision1[ii],
                                                   allowedError);
            }
          }

          void TestLBGKNNCalculationsAndCollision()
          {
            /*
             * When testing this streamer, it is important to consider that tau is defined per site.
             * Use two different sets of initial conditions across the domain to check that different
             * shear-rates and relaxation times are computed and stored properly.
             *
             * Using {f_, velocities}setA for odd site indices and {f_, velocities}setB for the even ones
             */
            distribn_t f_setA[D3Q15::NUMVECTORS], f_setB[D3Q15::NUMVECTORS];
            distribn_t* f_original;

            for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
            {
              f_setA[ii] = ((float) (1 + ii)) / 10.0;
              f_setB[ii] = ((float) (D3Q15::NUMVECTORS - ii)) / 10.0;
            }

            typedef lb::kernels::LBGKNN<lb::kernels::rheologyModels::CarreauYasudaRheologyModel> LB_KERNEL;
            lb::kernels::HydroVars<LB_KERNEL> hydroVars0SetA(f_setA), hydroVars1SetA(f_setA);
            lb::kernels::HydroVars<LB_KERNEL> hydroVars0SetB(f_setB), hydroVars1SetB(f_setB);
            lb::kernels::HydroVars<LB_KERNEL> *hydroVars0 = NULL, *hydroVars1 = NULL;

            distribn_t velocitiesSetA[] = { 0.4, 0.5, 0.6 };
            distribn_t velocitiesSetB[] = { -0.4, -0.5, -0.6 };
            distribn_t *velocities;

            distribn_t numTolerance = 1e-10;

            for (site_t site_index = 0; site_index < numSites; site_index++)
            {
              /*
               * Test part 1: Equilibrium function, density, and velocity are computed
               * identically to the standard LBGK. Local relaxation times are implicitely
               * computed by CalculateDensityVelocityFeq
               */

              /*
               * Case 0: test the kernel function for calculating density, velocity
               * and f_eq.
               * Case 1: test the function that uses a given density and velocity, and
               * calculates f_eq.
               */
              if (site_index % 2)
              {
                f_original = f_setA;
                hydroVars0 = &hydroVars0SetA;
                hydroVars1 = &hydroVars1SetA;
                velocities = velocitiesSetA;
              }
              else
              {
                f_original = f_setB;
                hydroVars0 = &hydroVars0SetB;
                hydroVars1 = &hydroVars1SetB;
                velocities = velocitiesSetB;
              }

              // Calculate density, velocity, equilibrium f.
              lbgknn0->CalculateDensityVelocityFeq(*hydroVars0, site_index);

              // Manually set density and velocity and calculate eqm f.
              hydroVars1->density = 1.0;
              hydroVars1->v_x = velocities[0];
              hydroVars1->v_y = velocities[1];
              hydroVars1->v_z = velocities[2];

              lbgknn1->CalculateFeq(*hydroVars1, site_index);

              // Calculate expected values.
              distribn_t expectedDensity0 = 12.0; // (sum 1 to 15) / 10
              distribn_t expectedDensity1 = 1.0; // Unchanged

              distribn_t expectedVelocity0[3];
              LbTestsHelper::CalculateVelocity<D3Q15>(hydroVars0->f, expectedVelocity0);
              distribn_t *expectedVelocity1 = velocities;

              distribn_t expectedFEq0[D3Q15::NUMVECTORS];
              LbTestsHelper::CalculateLBGKEqmF<D3Q15>(expectedDensity0,
                                                      expectedVelocity0[0],
                                                      expectedVelocity0[1],
                                                      expectedVelocity0[2],
                                                      expectedFEq0);
              distribn_t expectedFEq1[D3Q15::NUMVECTORS];
              LbTestsHelper::CalculateLBGKEqmF<D3Q15>(expectedDensity1,
                                                      expectedVelocity1[0],
                                                      expectedVelocity1[1],
                                                      expectedVelocity1[2],
                                                      expectedFEq1);

              // Now compare the expected and actual values.
              LbTestsHelper::CompareHydros(expectedDensity0,
                                           expectedVelocity0[0],
                                           expectedVelocity0[1],
                                           expectedVelocity0[2],
                                           expectedFEq0,
                                           "LBGKNN, case 0",
                                           *hydroVars0,
                                           numTolerance);
              LbTestsHelper::CompareHydros(expectedDensity1,
                                           expectedVelocity1[0],
                                           expectedVelocity1[1],
                                           expectedVelocity1[2],
                                           expectedFEq1,
                                           "LBGKNN, case 1",
                                           *hydroVars1,
                                           numTolerance);

              /*
               * Test part 2: Test that the array containing the local relaxation
               * times has the right length and test against some hardcoded values.
               * Correctness of the relaxation time calculator is tested in RheologyModelTest.h
               */

              /*
               * A second call to the Calculate* functions will make sure that the newly computed
               * tau is used in DoCollide as opposite to the default Newtonian tau used during the
               * first time step.
               */
              lbgknn0->CalculateDensityVelocityFeq(*hydroVars0, site_index);
              lbgknn1->CalculateFeq(*hydroVars1, site_index);

              distribn_t computedTau0 = hydroVars0->tau;
              CPPUNIT_ASSERT_EQUAL_MESSAGE("Tau array size ", numSites, (site_t) lbgknn0->GetTauValues().size());

              distribn_t expectedTau0 = site_index % 2
                ? 0.50009134451
                : 0.50009285237;

              std::stringstream message;
              message << "Tau array [" << site_index << "] for dataset 0";
              CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(message.str(),
                                                   expectedTau0,
                                                   computedTau0,
                                                   numTolerance);

              distribn_t computedTau1 = hydroVars1->tau;
              CPPUNIT_ASSERT_EQUAL_MESSAGE("Tau array size ", numSites, (site_t) lbgknn1->GetTauValues().size());

              distribn_t expectedTau1 = site_index % 2
                ? 0.50009013551
                : 0.50009021207;

              message.str("");
              message << "Tau array [" << site_index << "] for dataset 1";
              CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(message.str(),
                                                   expectedTau1,
                                                   computedTau1,
                                                   numTolerance);

              /*
               * Test part 3: Collision depends on the local relaxation time
               */
              // Do the collision and test the result.
              distribn_t postCollision0[D3Q15::NUMVECTORS];
              distribn_t postCollision1[D3Q15::NUMVECTORS];

              // Set the values in f_neq.
              for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
              {
                hydroVars0->f_neq[ii] = f_original[ii] - hydroVars0->f_eq[ii];
                hydroVars1->f_neq[ii] = f_original[ii] - hydroVars1->f_eq[ii];
              }

              for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
              {
                postCollision0[ii] = lbgknn0->DoCollide(lbmParams, *hydroVars0, ii);
                postCollision1[ii] = lbgknn1->DoCollide(lbmParams, *hydroVars1, ii);
              }

              // Get the expected post-collision densities.
              distribn_t expectedPostCollision0[D3Q15::NUMVECTORS];
              distribn_t expectedPostCollision1[D3Q15::NUMVECTORS];

              distribn_t localOmega0 = -1.0 / computedTau0;
              distribn_t localOmega1 = -1.0 / computedTau1;

              LbTestsHelper::CalculateLBGKCollision<D3Q15>(f_original,
                                                           hydroVars0->f_eq,
                                                           localOmega0,
                                                           expectedPostCollision0);

              LbTestsHelper::CalculateLBGKCollision<D3Q15>(f_original,
                                                           hydroVars1->f_eq,
                                                           localOmega1,
                                                           expectedPostCollision1);

              // Compare.
              for (unsigned int ii = 0; ii < D3Q15::NUMVECTORS; ++ii)
              {
                std::stringstream message;
                message << "Post-collision: site " << site_index << " direction " << ii;

                CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(message.str(),
                                                     postCollision0[ii],
                                                     expectedPostCollision0[ii],
                                                     numTolerance);

                CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(message.str(),
                                                     postCollision1[ii],
                                                     expectedPostCollision1[ii],
                                                     numTolerance);
              }
            }
          }

        private:
          geometry::LatticeData* latDat;
          lb::LbmParameters* lbmParams;
          lb::kernels::Entropic* entropic;
          lb::kernels::LBGK* lbgk;
          lb::kernels::LBGKNN<lb::kernels::rheologyModels::CarreauYasudaRheologyModel> *lbgknn0, *lbgknn1;
          site_t numSites;
      };

    }
  }
}

#endif /* HEMELB_UNITTESTS_LBTESTS_KERNELTESTS_H */