int main(int argc, char *argv[])
{
    if (argc > 1) {
        rangeMax = std::stoi(argv[1]);
    }
    
    // 设置随机数种子
    std::srand(std::time(0));

    // 启动生产者线程和消费者线程
    std::thread producer(producerThread);
    std::thread consumer(consumerThread);

    // 等待线程结束
    producer.join();
    consumer.join();

    return 0;
}
