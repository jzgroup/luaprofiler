import socket
import struct
import time
def start_server():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind(('0.0.0.0', 8080))
    server_socket.listen(5)
    print("Server started, waiting for connection...")
    messageTab = {}
    count = 0 
    while True:
        client_socket, addr = server_socket.accept()
        print(f"Connection from {addr} has been established!")
        
        try:
            while True:
                # 接收消息的前4个字节，解释为消息长度
                raw_msglen = client_socket.recv(4)
                if not raw_msglen:
                    break  # 客户端关闭连接或出现错误
                msglen = struct.unpack('!I', raw_msglen)[0]

                # 接收实际消息
                message_from_client = client_socket.recv(msglen).decode('utf-8')
        
                # 在这里可以根据:进行分割并处理消息内容
                segments = message_from_client.split("+")

                key = segments[1]
                #检查segments长度和内容
                if len(segments) > 1:
                    action = segments[0]
                    #根据：分割key
                    keyTab = key.split(":")
                    #key等于除了最后一位相加
                    key = ":".join(keyTab[:-1])

             
                    #最后一位是时间
                    current_time = float(keyTab[-1])
              
                    if key not in messageTab:
                        messageTab[key] = {
                            "count": 1,
                            "timeStack": [],
                            "totalTime": 0.0,
                        }

                    if action == "1":  # 开始
                        messageTab[key]["count"] += 1
                        messageTab[key]["timeStack"].append(current_time)
                    elif action == "0":  # 结束
                        if len(messageTab[key]["timeStack"]) > 0:
                            start_time = messageTab[key]["timeStack"].pop()
                            messageTab[key]["totalTime"] += current_time - start_time

                # 向客户端发送响应（可选）
                # message_to_client = "Received: " + message_from_client
                # client_socket.send(message_to_client.encode('utf-8'))
        except Exception as e:
            print(f"Error: {e}")
        finally:
            # print(f"Total messages received: {count}")
            client_socket.close()
            # # 打印统计数据
            # for key, data in messageTab.items():
            #     print(f"Key: {key}, Count: {data['count']}, Total Time: {data['totalTime']:.10f}s")

            #根据 count排序一下
            messageTab = dict(sorted(messageTab.items(), key=lambda x: x[1]['count'], reverse=True))
            for key, data in messageTab.items():
                # # 如果key有@不显示
                # if not key.startswith('@'):
                print(f"Key: {key}, Count: {data['count']}, Total Time: {data['totalTime']:.6f}s")
                # print(f"Key: {key}, Count: {data['count']}, Total Time: {data['totalTime']:.2f}s")
            messageTab = {}
            print(f"Connection from {addr} has been closed")


if __name__ == "__main__":
    start_server()