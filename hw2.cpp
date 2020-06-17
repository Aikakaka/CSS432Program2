/* 
    Program 2 Sliding windows
    CSS 432
    Aika Usui
    Implements the sliding window algorithm and evaluates its performance improvement 
    over a 1Gbps network.
 */
#include <iostream>
#include "UdpSocket.h"
#include "Timer.h"
using namespace std;

const int PORT = 12345;     // my UDP port
const int MAX = 20000;      // times of message transfer
const int MAX_WIN = 30;     // maximum window size
const bool verbose = false; //use verbose mode for more information during run
const int TIMEOUT = 1500;

// client packet sending functions
void ClientUnreliable(UdpSocket &sock, int max, int message[]);
int ClientStopWait(UdpSocket &sock, int max, int message[]);
int ClientSlidingWindow(UdpSocket &sock, int max, int message[], int windowSize);

// server packet receiving functions
void ServerUnreliable(UdpSocket &sock, int max, int message[]);
void ServerReliable(UdpSocket &sock, int max, int message[]);
void ServerEarlyRetrans(UdpSocket &sock, int max, int message[], int windowSize);

enum myPartType
{
    CLIENT,
    SERVER
} myPart;

int main(int argc, char *argv[])
{
    int message[MSGSIZE / 4]; // prepare a 1460-byte message: 1460/4 = 365 ints;

    // Parse arguments
    if (argc == 1)
    {
        myPart = SERVER;
    }
    else if (argc == 2)
    {
        myPart = CLIENT;
    }
    else
    {
        cerr << "usage: " << argv[0] << " [serverIpName]" << endl;
        return -1;
    }

    // Set up communication
    // Use different initial ports for client server to allow same box testing
    UdpSocket sock(PORT + myPart);
    if (myPart == CLIENT)
    {
        if (!sock.setDestAddress(argv[1], PORT + SERVER))
        {
            cerr << "cannot find the destination IP name: " << argv[1] << endl;
            return -1;
        }
    }

    int testNumber;
    cerr << "Choose a testcase" << endl;
    cerr << "   1: unreliable test" << endl;
    cerr << "   2: stop-and-wait test" << endl;
    cerr << "   3: sliding windows" << endl;
    cerr << "--> ";
    cin >> testNumber;

    if (myPart == CLIENT)
    {
        Timer timer;
        int retransmits = 0;

        switch (testNumber)
        {
        case 1:
            timer.Start();
            ClientUnreliable(sock, MAX, message);
            cout << "Elasped time = ";
            cout << timer.End() << endl;
            break;
        case 2:
            timer.Start();
            retransmits = ClientStopWait(sock, MAX, message);
            cout << "Elasped time = ";
            cout << timer.End() << endl;
            cout << "retransmits = " << retransmits << endl;
            break;
        case 3:
            for (int windowSize = 1; windowSize <= MAX_WIN; windowSize++)
            {
                timer.Start();
                retransmits = ClientSlidingWindow(sock, MAX, message, windowSize);
                cout << "Window size = ";
                cout << windowSize << " ";
                cout << "Elasped time = ";
                cout << timer.End() << endl;
                cout << "retransmits = " << retransmits << endl;
            }
            break;
        default:
            cerr << "no such test case" << endl;
            break;
        }
    }
    if (myPart == SERVER)
    {
        switch (testNumber)
        {
        case 1:
            ServerUnreliable(sock, MAX, message);
            break;
        case 2:
            ServerReliable(sock, MAX, message);
            break;
        case 3:
            for (int windowSize = 1; windowSize <= MAX_WIN; windowSize++)
            {
                ServerEarlyRetrans(sock, MAX, message, windowSize);
            }
            break;
        default:
            cerr << "no such test case" << endl;
            break;
        }

        // The server should make sure that the last ack has been delivered to client.
        if (testNumber != 1)
        {
            if (verbose)
            {
                cerr << "server ending..." << endl;
            }
            for (int i = 0; i < 10; i++)
            {
                sleep(1);
                int ack = MAX - 1;
                sock.ackTo((char *)&ack, sizeof(ack));
            }
        }
    }
    cout << "finished" << endl;
    return 0;
}

// Test 1 Client
void ClientUnreliable(UdpSocket &sock, int max, int message[])
{
    // transfer message[] max times; message contains sequences number i
    for (int i = 0; i < max; i++)
    {
        message[0] = i;
        sock.sendTo((char *)message, MSGSIZE);
        if (verbose)
        {
            cerr << "message = " << message[0] << endl;
        }
    }
    cout << max << " messages sent." << endl;
}

// Test1 Server
void ServerUnreliable(UdpSocket &sock, int max, int message[])
{
    // receive message[] max times and do not send ack
    for (int sequence = 0; sequence < max;)
    {
        sock.recvFrom((char *)message, MSGSIZE);
        if (verbose)
        {
            cerr << message[0] << endl;
        }
    }
    cout << max << " messages received" << endl;
}

// Test2 Client
//repeats sending message[] and receiving an acknowledgment at the client side
//max times using the sock object. If the client cannot receive an acknowledgment immediately,
//it should start a timer and wait 1500 usec. If the wait timeouts, the client should resend the same message.
//The function must count the number of messages retransmitted and return it to the main function as its return value.
int ClientStopWait(UdpSocket &sock, int max, int message[])
{
    int ack = 0;    //ack from server
    int retransmits = 0;
    int sequence = 0;
    Timer timer;
    for (sequence = 0; sequence < max;)
    {
        message[0] = sequence;
        // send message
        sock.sendTo((char *)message, MSGSIZE);

        timer.Start();

        while (sock.pollRecvFrom() <= 0)
        {
            if (timer.End() > TIMEOUT)
            {
                //resend
                sock.sendTo((char *)message, MSGSIZE);
                timer.Start();
                retransmits++;
            }
        }
        sock.recvFrom((char *)&ack, sizeof(ack));
        if (ack == sequence)
        {
            sequence++;
        }
    }
    return retransmits;
}

// Test3 Client
//repeats sending message[] and receiving an acknowledgment at a client side max times using
//the sock object. As described above, the client can continuously send a new message[] and
//incrementing its sequence number as long as the number of in-transit messages,
//(i.e., # of unacknowledged messages) is less than windowSize. If the # of unacknowledged messages
//reaches windowSize, the client should start a timer for 1500usec. If the timer timeouts,
//it must follow the sliding window algorithm and resend the message with the minimum sequence number
//among those which have not yet been acknowledged. The function must count the number of messages retransmitted and
//return it to the main function as its return value.
int ClientSlidingWindow(UdpSocket &sock, int max, int message[], int windowSize)
{
    int retransmits = 0;
    int ack;    //ack from server
    int ackSeq = 0; //cumulative ack
    int sequence = 0;
    Timer timer;

    for (sequence = 0; sequence < max || ackSeq < max;)
    {
        //within the sliding window
        if (ackSeq + windowSize > sequence && sequence < max)
        {
            message[0] = sequence;
            //send message
            sock.sendTo((char *)message, MSGSIZE);
            timer.Start();
            if (sock.pollRecvFrom() > 0)
            {
                sock.recvFrom((char *)&ack, sizeof(ack));
                if (ack == ackSeq)
                {
                    ackSeq++;
                }
            }
            sequence++;
        }
        else
        {
            if (ackSeq >= max)
            {
                break;
            }
            if (timer.End() > TIMEOUT)
            {
                //Restart timer for resent message
                timer.Start();
                sock.sendTo((char *)message, MSGSIZE);
                //Increase retransmit
                retransmits++;
            }
            //the sliding window is full
            //until a timeout(1500 msecs) occurs, keep calling sock.pollRecvFrom()
            while (timer.End() <= TIMEOUT)
            {
                sock.pollRecvFrom();
                sock.recvFrom((char *)&ack, sizeof(ack));
                if (ack >= ackSeq)
                {
                    ackSeq = ack + 1;
                    break;
                }
                else
                {
                    //resend
                    timer.Start();
                    sock.sendTo((char *)message, MSGSIZE);
                    retransmits++;
                }
            }
        }
    }
    return retransmits;
}

//Test2 Server
//repeats receiving message[] and sending an acknowledgment
// at a server side max times using the sock object.
void ServerReliable(UdpSocket &sock, int max, int message[])
{
    int sequence = 0;
    int ack = 0;
    for (sequence = 0; sequence < max;)
    {
        do
        {
            sock.recvFrom((char *)&ack, sizeof(ack));
            // check if this is a message expected to receive
            if (ack == sequence)
            {
                //send back an ack with this sequence number
                sock.ackTo((char *)&sequence, sizeof(sequence));
                sequence++;
                break;
            }
            else
            {
                break;
            }

        } while (ack != sequence);
    }
}

//Test3 Server
//repeats receiving message[] and sending an acknowledgment at a server side max times
//using the sock object. Every time the server receives a new message[],
//it must record this message's sequence number in its array and returns a cumulative acknowledgment.
void ServerEarlyRetrans(UdpSocket &sock, int max, int message[], int windowSize)
{
    int ack = 0;    //ack message
    bool array[max];    //received or not
    int sequence = 0;   // sequence number of expected message 
    //initialize the array. No message has arrived.
    for (int i = 0; i < max; i++)
    {
        array[i] = false;
    }
    for (sequence = 0; sequence < max;)
    {
        sock.recvFrom((char *)message, MSGSIZE);
        //if ack and ACKsequence equal
        if (message[0] == sequence)
        {
            //mark array[sequence] and
            array[sequence] = true;
            //scan array[sequence] to find last true element (cumulative ack)
            for (int j = max - 1; j >= 0; j--)
            {
                if (array[j] == true) //advance sequence to the last consecutive true element’s index+1.
                {
                    ack = j;
                    sequence = j + 1;
                    break;
                }
            }
        }
        else
        {
            //mark array[message[0]] as being received
            array[message[0]] = true;
        }
        // send back the cumulative ack for the series of messages received so far
        sock.ackTo((char *)&ack, sizeof(ack));
    }
}
