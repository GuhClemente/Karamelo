# Karamelo Live - internet chat rooms. See docs/CHAT_SERVER.md.
FROM node:20-alpine
ENV NODE_ENV=production
ENV PORT=8787
ENV CHAT_DATA_DIR=/data
ENV CHAT_TRUST_PROXY=1
WORKDIR /app
COPY server.mjs ./
# The ban list: mount a volume here or bans are lost with the container.
RUN mkdir -p /data && chown node:node /data
USER node
EXPOSE 8787
HEALTHCHECK --interval=30s --timeout=5s CMD wget -qO- http://127.0.0.1:8787/healthz || exit 1
CMD ["node", "server.mjs"]
