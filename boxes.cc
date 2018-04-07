/*
 * Copyright (C) 2015 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
#include <string>

#include "gazebo/msgs/msgs.hh"
#include "gazebo/physics/physics.hh"
#include "boxes.hh"

using namespace gazebo;
using namespace benchmark;

/////////////////////////////////////////////////
// Boxes:
// Spawn a single box and record accuracy for momentum and enery
// conservation
void BoxesTest::Boxes(const std::string &_physicsEngine
                    , double _dt
                    , int _modelCount
                    , bool _collision
                    , bool _complex)
{
  // Load a blank world (no ground plane)
  Load("worlds/blank.world", true, _physicsEngine);
  physics::WorldPtr world = physics::get_world("default");
  ASSERT_NE(world, nullptr);

  // Verify physics engine type
  physics::PhysicsEnginePtr physics = world->Physics();
  ASSERT_NE(physics, nullptr);
  ASSERT_EQ(physics->GetType(), _physicsEngine);

  // get gravity value
  if (!_complex)
  {
    physics->SetGravity(ignition::math::Vector3d::Zero);
  }
  ignition::math::Vector3d g = world->Gravity();

  // Box size
  const double dx = 0.1;
  const double dy = 0.4;
  const double dz = 0.9;
  const double mass = 10.0;
  // expected inertia matrix, recompute if the above change
  const double Ixx = 0.80833333;
  const double Iyy = 0.68333333;
  const double Izz = 0.14166667;
  const ignition::math::Matrix3d I0(Ixx, 0.0, 0.0
                                  , 0.0, Iyy, 0.0
                                  , 0.0, 0.0, Izz);

  // Create box with inertia based on box of uniform density
  msgs::Model msgModel;
  msgs::AddBoxLink(msgModel, mass, ignition::math::Vector3d(dx, dy, dz));
  if (!_collision)
  {
    // Test without collision shapes.
    msgModel.mutable_link(0)->clear_collision();
  }

  // spawn multiple boxes
  // compute error statistics only on the last box
  ASSERT_GT(_modelCount, 0);
  physics::ModelPtr model;
  physics::LinkPtr link;

  // initial linear velocity in global frame
  ignition::math::Vector3d v0;

  // initial angular velocity in global frame
  ignition::math::Vector3d w0;

  // initial energy value
  double E0;

  if (!_complex)
  {
    v0.Set(-0.9, 0.4, 0.1);
    // Use angular velocity with one non-zero component
    // to ensure linear angular trajectory
    w0.Set(0.5, 0, 0);
    E0 = 5.001041625;
  }
  else
  {
    v0.Set(-2.0, 2.0, 8.0);
    // Since Ixx > Iyy > Izz,
    // angular velocity with large y component
    // will cause gyroscopic tumbling
    w0.Set(0.1, 5.0, 0.1);
    E0 = 368.54641249999997;
  }

  for (int i = 0; i < _modelCount; ++i)
  {
    // give models unique names
    msgModel.set_name(this->GetUniqueString("model"));
    // give models unique positions
    msgs::Set(msgModel.mutable_pose()->mutable_position(),
              ignition::math::Vector3d(0.0, dz*2*i, 0.0));

    model = this->SpawnModel(msgModel);
    ASSERT_NE(model, nullptr);

    link = model->GetLink();
    ASSERT_NE(link, nullptr);

    // Set initial conditions
    link->SetLinearVel(v0);
    link->SetAngularVel(w0);
  }
#if GAZEBO_MAJOR_VERSION >= 8
  ASSERT_EQ(v0, link->WorldCoGLinearVel());
  ASSERT_EQ(w0, link->WorldAngularVel());
  ASSERT_EQ(I0, link->GetInertial()->MOI());
#else
  ASSERT_EQ(v0, link->GetWorldCoGLinearVel().Ign());
  ASSERT_EQ(w0, link->GetWorldAngularVel().Ign());
  ASSERT_EQ(I0, link->GetInertial()->GetMOI());
#endif
  ASSERT_NEAR(link->GetWorldEnergy(), E0, 1e-6);

#if GAZEBO_MAJOR_VERSION >= 8
  // initial time
  common::Time t0 = world->SimTime();

  // initial linear position in global frame
  ignition::math::Vector3d p0 = link->WorldInertialPose().Pos();

  // initial angular momentum in global frame
  ignition::math::Vector3d H0 = link->WorldAngularMomentum();
#else
  // initial time
  common::Time t0 = world->GetSimTime();

  // initial linear position in global frame
  ignition::math::Vector3d p0 = link->GetWorldInertialPose().Ign().Pos();

  // initial angular momentum in global frame
  ignition::math::Vector3d H0 = link->GetWorldAngularMomentum().Ign();
#endif
  ASSERT_EQ(H0, ignition::math::Vector3d(Ixx, Iyy, Izz) * w0);
  double H0mag = H0.Length();

  // change step size after setting initial conditions
  // since simbody requires a time step
  physics->SetMaxStepSize(_dt);
  const double simDuration = 10.0;
  int steps = ceil(simDuration / _dt);

  // variables to compute statistics on
  ignition::math::Vector3Stats linearPositionError;
  ignition::math::Vector3Stats linearVelocityError;
  ignition::math::Vector3Stats angularPositionError;
  ignition::math::Vector3Stats angularMomentumError;
  ignition::math::SignalStats energyError;
  {
    const std::string statNames = "maxAbs";
    EXPECT_TRUE(linearPositionError.InsertStatistics(statNames));
    EXPECT_TRUE(linearVelocityError.InsertStatistics(statNames));
    EXPECT_TRUE(angularPositionError.InsertStatistics(statNames));
    EXPECT_TRUE(angularMomentumError.InsertStatistics(statNames));
    EXPECT_TRUE(energyError.InsertStatistics(statNames));
  }

  // unthrottle update rate
  physics->SetRealTimeUpdateRate(0.0);
  common::Time startTime = common::Time::GetWallTime();
  for (int i = 0; i < steps; ++i)
  {
    world->Step(1);

    // current time
#if GAZEBO_MAJOR_VERSION >= 8
    double t = (world->SimTime() - t0).Double();
#else
    double t = (world->GetSimTime() - t0).Double();
#endif

    // linear velocity error
#if GAZEBO_MAJOR_VERSION >= 8
    ignition::math::Vector3d v = link->WorldCoGLinearVel();
#else
    ignition::math::Vector3d v = link->GetWorldCoGLinearVel().Ign();
#endif
    linearVelocityError.InsertData(v - (v0 + g*t));

    // linear position error
#if GAZEBO_MAJOR_VERSION >= 8
    ignition::math::Vector3d p = link->WorldInertialPose().Pos();
#else
    ignition::math::Vector3d p = link->GetWorldInertialPose().Ign().Pos();
#endif
    linearPositionError.InsertData(p - (p0 + v0 * t + 0.5*g*t*t));

    // angular momentum error
#if GAZEBO_MAJOR_VERSION >= 8
    ignition::math::Vector3d H = link->WorldAngularMomentum();
#else
    ignition::math::Vector3d H = link->GetWorldAngularMomentum().Ign();
#endif
    angularMomentumError.InsertData((H - H0) / H0mag);

    // angular position error
    if (!_complex)
    {
#if GAZEBO_MAJOR_VERSION >= 8
      ignition::math::Vector3d a = link->WorldInertialPose().Rot().Euler();
#else
      ignition::math::Vector3d a = link->GetWorldInertialPose().Ign().Rot().Euler();
#endif
      ignition::math::Quaterniond angleTrue(w0 * t);
      angularPositionError.InsertData(a - angleTrue.Euler());
    }

    // energy error
    energyError.InsertData((link->GetWorldEnergy() - E0) / E0);
  }
  common::Time elapsedTime = common::Time::GetWallTime() - startTime;
  this->Record("wallTime", elapsedTime.Double());
  common::Time simTime = (world->SimTime() - t0).Double();
  ASSERT_NEAR(simTime.Double(), simDuration, _dt*1.1);
  this->Record("simTime", simTime.Double());
  this->Record("timeRatio", elapsedTime.Double() / simTime.Double());

  // Record statistics on pitch and yaw angles
  this->Record("energy0", E0);
  this->Record("energyError_", energyError);
  this->Record("angMomentum0", H0mag);
  this->Record("angMomentumErr_", angularMomentumError.Mag());
  this->Record("angPositionErr", angularPositionError);
  this->Record("linPositionErr_", linearPositionError.Mag());
  this->Record("linVelocityErr_", linearVelocityError.Mag());
}

/////////////////////////////////////////////////
TEST_P(BoxesTest, Boxes)
{
  std::string physicsEngine = std::tr1::get<0>(GetParam());
  double dt                 = std::tr1::get<1>(GetParam());
  int modelCount            = std::tr1::get<2>(GetParam());
  bool collision            = std::tr1::get<3>(GetParam());
  bool isComplex            = std::tr1::get<4>(GetParam());
  gzdbg << physicsEngine
        << ", dt: " << dt
        << ", modelCount: " << modelCount
        << ", collision: " << collision
        << ", isComplex: " << isComplex
        << std::endl;
  RecordProperty("engine", physicsEngine);
  this->Record("dt", dt);
  RecordProperty("modelCount", modelCount);
  RecordProperty("collision", collision);
  RecordProperty("isComplex", isComplex);
  Boxes(physicsEngine
      , dt
      , modelCount
      , collision
      , isComplex);
}
