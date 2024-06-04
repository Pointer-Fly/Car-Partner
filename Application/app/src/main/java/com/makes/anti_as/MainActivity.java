package com.makes.anti_as;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;

import static android.content.ContentValues.TAG;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;
import androidx.core.content.ContextCompat;

import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.util.Log;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken;
import org.eclipse.paho.client.mqttv3.MqttCallback;
import org.eclipse.paho.client.mqttv3.MqttClient;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttException;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence;
import org.w3c.dom.Text;

import android.Manifest;

import java.math.BigDecimal;
import java.util.Timer;
import java.util.TimerTask;

public class MainActivity extends AppCompatActivity {
    private static final int REQUEST_CODE_NOTIFICATION = 10;
    private static final int REQUEST_NOTIFICATION_PERMISSION = 1;

    private Boolean isVibration = false;
    private Timer vibrationTimer = new Timer();
    private String serverUri = "tcp://124.222.71.199:1883";    //这里可以填上各种云平台的物联网云平台的域名+1883端口号，什么阿里云腾讯云百度云天工物接入都可以，
    // 这里我填的是我在我的阿里云服务器上搭建的EMQ平台的地址，
    // 注意：前缀“tcp：//”不可少，之前我没写，怎么都连不上，折腾了好久
    private String userName = "android_esp32_anti-as-app";                    //然后是你的用户名，阿里云腾讯云百度云天工物接入这些平台你新建设备后就自动生成了
    private String passWord = "1234567";                    //用户名对应的密码，同样各种云平台都会对应生成密码，这里我的EMQ平台没做限制，所以用户名和密码可以随便填写
    private String clientId = "anti" + System.currentTimeMillis(); //clientId很重要，不能重复，否则就会连不上，所以我定义成 app+当前时间
    private String mqtt_sub_topic = "/order/anti/publish";          //需要订阅的主题
    private String mqtt_pub_topic = "/order/anti/subscribe";          //需要发布的主题

    private String CurrentEW = "E";
    private String CurrentNS = "N";

    private String formattedTime = "0";

    private Boolean isNotificate = true;

    private void makeToast(String toast_str) {
        Toast.makeText(MainActivity.this, toast_str, Toast.LENGTH_LONG).show();
    }

    public Handler handler;
    private MqttClient mqtt_client;                         //创建一个mqtt_client对象

    private int temperature = 0;
    private int humidity = 0;
    private int Co2 = 0;
    private double latitude = 0;
    private double longitude = 0;

    public void mqtt_init_Connect() {
        try {
            //实例化mqtt_client，填入我们定义的serverUri和clientId，然后MemoryPersistence设置clientid的保存形式，默认为以内存保存
            mqtt_client = new MqttClient(serverUri, clientId, new MemoryPersistence());
            //创建并实例化一个MQTT的连接参数对象
            MqttConnectOptions options = new MqttConnectOptions();
            //然后设置对应的参数
            options.setConnectionTimeout(30);               // 设置超时时间，单位为秒
            options.setKeepAliveInterval(50);               //设置心跳,30s
//            options.setAutomaticReconnect(true);            //是否重连
            //设置是否清空session,设置为false表示服务器会保留客户端的连接记录，设置为true表示每次连接到服务器都以新的身份连接
            options.setCleanSession(false);

            //设置回调
            mqtt_client.setCallback(new MqttCallback() {
                @Override
                public void connectionLost(Throwable cause) {
                    //连接丢失后，一般在这里面进行重连
                }

                @Override
                public void deliveryComplete(IMqttDeliveryToken token) {
                    //publish后会执行到这里
                    System.out.println("deliveryComplete");
                }

                @Override
                public void messageArrived(String topicName, MqttMessage message) throws Exception {
//                    DealMessage(topicName, message);
                    //subscribe后得到的消息会执行到这里面
                    String msg = new String(message.getPayload());
                    System.out.println("messageArrived:" + msg);
                    DealMessage(msg);
                }
            });
            //连接mqtt服务器
            mqtt_client.connect();
            // 创建线程监听MQTT是否连接，如果断开连接则重新连接
            new Thread(new Runnable() {
                @Override
                public void run() {
                    while (true) {
                        try {
                            Thread.sleep(1000);
                            if (!mqtt_client.isConnected()) {
                                mqtt_client.disconnect();
                                mqtt_client.connect();
                                if(mqtt_client.isConnected()){
                                    mqtt_client.subscribe(mqtt_sub_topic, 0);
                                }
                            }
                        } catch (Exception e) {
                            e.printStackTrace();
                        }
                    }
                }
            }).start();
            mqtt_client.subscribe(mqtt_sub_topic, 0);
        } catch (Exception e) {
            e.printStackTrace();
            makeToast(e.toString());
        }
    }


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        createNotificationChannel();
        checkNotificationPermission();
        // 新建线程 刷新textview
        new Thread(new Runnable() {
            @Override
            public void run() {
                while (true) {
                    try {
                        Thread.sleep(1000);
                        TextView textTempratureValue = (TextView) findViewById(R.id.textTempratureValue);
                        TextView textHumidityValue = (TextView) findViewById(R.id.textHumidityValue);
                        TextView textCO2Value = (TextView) findViewById(R.id.textCO2Value);
                        TextView textGPSValue = (TextView) findViewById(R.id.textGPSValue);
                        TextView textNotification = (TextView) findViewById(R.id.textNotification);
                        String tempString;
                        if(temperature > 30)
                        {
                            tempString = "Temprature: " + temperature + "℃" + " Hight Temperature";
                        }else{
                            tempString = "Temprature: " + temperature + "℃";
                        }
                        String humiString = "Humidity: " + humidity + "%";
                        String co2String = "CO2: " + Co2;
                        String gpsString = "GPS: " + String.format("%.2f", latitude) + CurrentNS + " " + String.format("%.2f", longitude) + CurrentEW;
                        textTempratureValue.setText(tempString);
                        textHumidityValue.setText(humiString);
                        textCO2Value.setText(co2String);
                        textGPSValue.setText(gpsString);
                        String textNotificationValue = isNotificate? "Notification State: " + "Yes" :"Notification State: " + "No";
                        textNotification.setText(textNotificationValue);
                    } catch (Exception e) {
                        e.printStackTrace();
                    }
                }
            }
        }).start();

        mqtt_init_Connect();
    }

    /**
     * 订阅特定的主题
     * @param topic mqtt主题
     */
    public void subscribeTopic(String topic) {
        try {
            mqtt_client.subscribe(topic, 0);
        } catch (MqttException e) {
            e.printStackTrace();
        }
    } /* subscribeTopic */

    // Helper method to format milliseconds to a human-readable format (minutes:seconds)
    private String formatTime(long milliseconds) {
        int seconds = (int) (milliseconds / 1000) % 60;
        int minutes = (int) ((milliseconds / (1000 * 60)) % 60);
        return String.format("%d:%02d", minutes, seconds);
    }
    private void handleVibration() {
        isVibration = true;

        // Cancel any existing timer task
        vibrationTimer.cancel();
        vibrationTimer = new Timer();
//        long startTime = System.currentTimeMillis();

        // Schedule a new timer task to reset isVibration after 10 minutes
        TimerTask resetVibrationTask = new TimerTask() {
            @Override
            public void run() {
//                long elapsedTime = System.currentTimeMillis() - startTime;
//                long remainingTime = (10 * 60 * 1000) - elapsedTime;
//                formattedTime = formatTime(remainingTime);
                isVibration = false;
//                System.out.println("Vibration state reset to false.");
            }
        };
        vibrationTimer.schedule(resetVibrationTask, 1 * 15 * 1000); // 10 minutes
        // 获取当前剩余时间


        System.out.println("Vibration detected, timer reset.");
    }
    public void DealMessage(String msg) {
        // CO2:255,temperature:25,humidity:49,:,,:
        // CO2:255,temperature:25,humidity:49,:E,3728.64286,N:12127.39473
        String[] msgArray = msg.split(",");  // Split by spaces instead of commas
        if(msg.contains("hw") && isNotificate)
        {
            sendNotification("Infrared", "Infrared sensing data", 1);
        } else if (msg.contains("Vibration") && isNotificate) {
//            handleVibration();
            sendNotification("Vibration", "Vibration sensing data", 2);
        }

        try {
            for (String item : msgArray) {
                // Split each item into key and value
                if (item.contains(":") == false) {
                    continue;
                }
                String[] keyValuePair = item.split(":");
                String key = keyValuePair[0].trim();
                String value = keyValuePair[1].trim();
                if(key.contains("E"))
                {
                    latitude = Double.parseDouble(value)/100;
                    CurrentEW = "E";
                }else if(key.contains("W"))
                {
                    latitude = Double.parseDouble(value)/100;
                    CurrentEW = "W";
                }else if(key.contains("N"))
                {
                    longitude = Double.parseDouble(value)/100;
                    CurrentNS = "N";
                }else if(key.contains("S"))
                {
                    longitude = Double.parseDouble(value)/100;
                    CurrentNS = "W";
                }

                // Parse the value based on the key
                switch (key) {
                    case "CO2":
                        Co2 = Integer.parseInt(value);
                        // Do something with the CO2 level
                        break;
                    case "temperature":
                        temperature = Integer.parseInt(value);
                        // Do something with the temperature
                        break;
                    case "humidity":
                        humidity = Integer.parseInt(value);
                        // Do something with the humidity
                        break;
                    case "E":
                        latitude = Double.parseDouble(value)/100;
                        CurrentEW = "E";
                        // Do something with the latitude
                        break;
                    case "W":
                        latitude = Double.parseDouble(value)/100;
                        CurrentEW = "W";
                        // Do something with the latitude
                        break;
                    case "N":
                        longitude = Double.parseDouble(value)/100;
                        CurrentNS = "N";
                        // 保留两位
                        // Do something with the longitude
                        break;
                    case "S":
                        longitude = Double.parseDouble(value)/100;
                        CurrentNS = "S";
                    default:
                        // Handle unknown keys if necessary
                        break;
                }
            }
        } catch (Exception e) {
//                        makeToast(e.toString());
        }
    }


    public void Reconnecte(View view) {
        try {
            mqtt_client.connect();
            makeToast("重新连接成功");
        } catch (MqttException e) {
            e.printStackTrace();
            makeToast("重新连接失败");
        }
    }

    public void OpenLED(View view) throws MqttException {
        mqtt_client.publish(mqtt_pub_topic, new MqttMessage("led on".getBytes()));
        makeToast("Send Command Success");
    }

    public void CloseLED(View view) throws MqttException {
        mqtt_client.publish(mqtt_pub_topic, new MqttMessage("led off".getBytes()));
        makeToast("Send Command Success");
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            CharSequence name = "MyChannel";
            String description = "Channel for MyApp notifications";
            int importance = NotificationManager.IMPORTANCE_DEFAULT;
            NotificationChannel channel = new NotificationChannel("10", name, importance);
            channel.setDescription(description);

            NotificationManager notificationManager = getSystemService(NotificationManager.class);
            notificationManager.createNotificationChannel(channel);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_NOTIFICATION_PERMISSION) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            } else {
                // 权限被拒绝，处理逻辑
            }
        }
    }

    private void checkNotificationPermission() {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQUEST_NOTIFICATION_PERMISSION);
        } else {
        }
    }

    private void sendNotification(String title, String msg, int id) {
        Intent intent = new Intent(this, MainActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_IMMUTABLE);

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, "my_channel_id")
                .setSmallIcon(R.mipmap.ic_launcher)
                .setContentTitle(title)
                .setContentText(msg)
                .setPriority(NotificationCompat.PRIORITY_DEFAULT)
                .setContentIntent(pendingIntent)
                .setAutoCancel(true);

        NotificationManagerCompat notificationManager = NotificationManagerCompat.from(this);
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            // TODO: Consider calling
            //    ActivityCompat#requestPermissions
            // here to request the missing permissions, and then overriding
            //   public void onRequestPermissionsResult(int requestCode, String[] permissions,
            //                                          int[] grantResults)
            // to handle the case where the user grants the permission. See the documentation
            // for ActivityCompat#requestPermissions for more details.
//            return;
        }
        notificationManager.notify(id, builder.build());
    }

    public void OpenNotification(View view)
    {
        isNotificate = true;
    }

    public void CloseNotification(View view)
    {
        isNotificate = false;
    }
}