FROM sigoden/dufs:v0.46.0 AS dufs
FROM alpine:3.21
RUN apk add --no-cache wget
COPY --from=dufs /bin/dufs /bin/dufs
COPY entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/bin/sh", "/entrypoint.sh"]
