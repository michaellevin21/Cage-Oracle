FROM node:22-bookworm-slim AS frontend
WORKDIR /src/frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ ./
RUN npm run build

FROM debian:bookworm-slim AS cpp-build
RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake g++ libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include/ include/
COPY src/ src/
COPY third_party/ third_party/
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

FROM debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends libsqlite3-0 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=cpp-build /src/build/ufc_server_app ./ufc_server_app
COPY --from=frontend /src/frontend/dist ./frontend/dist
COPY ufc.db ./ufc.db
ENV PORT=8000
EXPOSE 8000
CMD ["./ufc_server_app", "--static", "frontend/dist"]
