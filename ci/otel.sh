#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

set -e

sudo apt-get update

# OTel dependencies

sudo apt-get install libcurl4-openssl-dev
# sudo apt-get install libboost-locale-dev
# sudo apt-get install libthrift-dev
# sudo apt-get install libprotobuf-dev

# Install protobuf

sudo apt-get install g++
sudo apt install curl gnupg
curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor > bazel.gpg
sudo mv bazel.gpg /etc/apt/trusted.gpg.d/
sudo apt update && sudo apt install bazel

git clone https://github.com/protocolbuffers/protobuf.git
cd protobuf
git submodule update --init --recursive
bazel build :protoc :protobuf

cd ..


# Clone OpenTelemetry-cpp

git clone -b v1.9.0 --recurse-submodules https://github.com/open-telemetry/opentelemetry-cpp

# Build/Install OpenTelemetry-cpp

cd opentelemetry-cpp
mkdir build
cd build

cmake ..  -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_SHARED_LIBS=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_OTLP_HTTP=ON
cmake --build . --target all
sudo cmake --install . --config RelWithDebInfo
cd ../..
