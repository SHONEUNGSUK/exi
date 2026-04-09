FROM node:20-slim
RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends gcc make zlib1g-dev ca-certificates && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN bash scripts/build_codec.sh
ENV NODE_ENV=production PORT=3000
EXPOSE 3000
HEALTHCHECK --interval=30s --timeout=5s \
  CMD node -e "require('http').get('http://localhost:3000/api/health',r=>process.exit(r.statusCode===200?0:1))"
CMD ["node","server/app.js"]
