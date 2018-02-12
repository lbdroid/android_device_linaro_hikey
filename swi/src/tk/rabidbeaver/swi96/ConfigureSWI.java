package tk.rabidbeaver.swi96;

import android.app.Activity;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;

import java.io.DataInputStream;
import java.io.DataOutputStream;

public class ConfigureSWI extends Activity {
    LocalSocket mSocket;
    DataInputStream is;
    DataOutputStream os;

    EditText inputbox;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_configure_swi);

        mSocket = new LocalSocket();

        try {
            mSocket.connect(new LocalSocketAddress("/dev/swi", LocalSocketAddress.Namespace.FILESYSTEM));
            is = new DataInputStream(mSocket.getInputStream());
            os = new DataOutputStream(mSocket.getOutputStream());
        } catch (Exception e){
            e.printStackTrace();
            this.finish();
        }

        Button startbtn = findViewById(R.id.startbtn);
        startbtn.setOnClickListener(new Button.OnClickListener(){
            @Override
            public void onClick(View btn){
                try {
                    os.writeBytes("P");
                } catch (Exception e){}
            }
        });

        Button stopbtn = findViewById(R.id.endbtn);
        stopbtn.setOnClickListener(new Button.OnClickListener(){
            @Override
            public void onClick(View btn){
                try {
                    os.writeBytes("E");
                } catch (Exception e){}
            }
        });

        Button reconnect = findViewById(R.id.reconnect);
        reconnect.setOnClickListener(new Button.OnClickListener(){
            @Override
            public void onClick(View btn){
                mSocket = new LocalSocket();

                try {
                    mSocket.connect(new LocalSocketAddress("/dev/swi", LocalSocketAddress.Namespace.FILESYSTEM));
                    is = new DataInputStream(mSocket.getInputStream());
                    os = new DataOutputStream(mSocket.getOutputStream());
                } catch (Exception e){
                    e.printStackTrace();
                }
            }
        });

        inputbox = findViewById(R.id.input);

        Button savebtn = findViewById(R.id.savebtn);
        savebtn.setOnClickListener(new Button.OnClickListener(){
            @Override
            public void onClick(View btn){
                int value = Integer.valueOf(inputbox.getText().toString());
                try {
                    os.writeByte(value);
                } catch (Exception e){}
            }
        });
    }
}
