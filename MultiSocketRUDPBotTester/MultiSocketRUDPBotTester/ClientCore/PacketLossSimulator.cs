namespace MultiSocketRUDPBotTester.ClientCore
{
  public sealed class PacketLossSimulator(double inLossRate, int inSeed)
  {
    private readonly double lossRate = inLossRate;
    private readonly Random receiveRandom = new(inSeed);
    private readonly Random sendRandom = new(inSeed ^ unchecked((int)0x9E6779B9));
    private readonly Lock receiveLock = new();
    private readonly Lock sendLock = new();
    private volatile bool isEnabled;

    public void SetEnabled(bool inEnabled)
    {
      isEnabled = inEnabled;
    }

    public bool ShouldDropReceivedDatagram()
    {
      if (!isEnabled)
      {
        return false;
      }

      lock (receivedLock)
      {
        return receiveRandom.NextDouble() < lossRate;
      }
    }

    public bool ShouldDropSendingDatagram()
    {
      if (!isEnabled)
      {
        return false;
      }

      lock (sendLock)
      {
        return sendRandom.NextDouble() < lossRate;
      }
    }
  }
}
