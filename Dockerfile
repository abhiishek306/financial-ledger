# --- Build stage -------------------------------------------------------
FROM ubuntu:24.04 AS build

RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
    cmake g++-13 ninja-build ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=g++-13 \
      -DLEDGER_ENABLE_SANITIZERS=OFF \
      -DLEDGER_BUILD_TESTS=ON \
      -DLEDGER_BUILD_BENCHMARKS=OFF \
    && cmake --build build -j \
    && ctest --test-dir build --output-on-failure

# --- Runtime stage -------------------------------------------------------
FROM ubuntu:24.04 AS runtime

RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/ledger_demo /usr/local/bin/ledger_demo

ENTRYPOINT ["/usr/local/bin/ledger_demo"]
