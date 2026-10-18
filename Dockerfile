FROM gcc:14-bookworm
WORKDIR /workspace
COPY . .
RUN make test && make demo
CMD ["make", "test"]
