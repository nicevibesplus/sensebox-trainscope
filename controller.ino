// Pin definition
const unsigned int IN1 = 14;
const unsigned int IN2 = 48;
const unsigned int EN_5V = 21;

void setup()
{
  pinMode(EN_5V, OUTPUT);
  digitalWrite(EN_5V, HIGH);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  
  // Used to display information
  Serial.begin(9600);

  // Wait for Serial Monitor to be opened
  while (!Serial)
  {
    //do nothing
  }
}

void loop()
{
  Serial.println("Direction: Forward, fast");
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  delay(3000);
}
