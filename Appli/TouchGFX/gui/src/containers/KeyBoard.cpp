#include <gui/containers/KeyBoard.hpp>
#include <string.h>

KeyBoard::KeyBoard()
    : bufLen(0),
      enterCallback(0),
      bufferChangedCallback(0),
      cbN0(this, &KeyBoard::onN0),
      cbN1(this, &KeyBoard::onN1),
      cbN2(this, &KeyBoard::onN2),
      cbN3(this, &KeyBoard::onN3),
      cbN4(this, &KeyBoard::onN4),
      cbN5(this, &KeyBoard::onN5),
      cbN6(this, &KeyBoard::onN6),
      cbN7(this, &KeyBoard::onN7),
      cbN8(this, &KeyBoard::onN8),
      cbN9(this, &KeyBoard::onN9),
      cbMinus(this, &KeyBoard::onMinus),
      cbDot(this, &KeyBoard::onDot),
      cbDel(this, &KeyBoard::onDel),
      cbEnter(this, &KeyBoard::onEnter)
{
    memset(inputBuf, 0, sizeof(inputBuf));
}

void KeyBoard::initialize()
{
    KeyBoardBase::initialize();

    N0.setAction(cbN0);
    N1.setAction(cbN1);
    N2.setAction(cbN2);
    N3.setAction(cbN3);
    N4.setAction(cbN4);
    N5.setAction(cbN5);
    N6.setAction(cbN6);
    N7.setAction(cbN7);
    N8.setAction(cbN8);
    N9.setAction(cbN9);
    Minus.setAction(cbMinus);     // − 키
    Minus_1.setAction(cbDot);     // 소수점 키
    Minus_1_1.setAction(cbDel);   // Del 키
    ent.setAction(cbEnter);       // Enter 키
}

void KeyBoard::clearBuffer()
{
    bufLen = 0;
    memset(inputBuf, 0, sizeof(inputBuf));
    notifyChanged();
}

void KeyBoard::appendChar(char c)
{
    if (bufLen >= MAX_BUF - 1)
    {
        return;
    }
    inputBuf[bufLen++] = c;
    inputBuf[bufLen]   = '\0';
    notifyChanged();
}

void KeyBoard::deleteLast()
{
    if (bufLen > 0)
    {
        inputBuf[--bufLen] = '\0';
        notifyChanged();
    }
}

void KeyBoard::notifyChanged()
{
    if (bufferChangedCallback != 0 && bufferChangedCallback->isValid())
    {
        bufferChangedCallback->execute(inputBuf);
    }
}

void KeyBoard::onMinus(const touchgfx::AbstractButtonContainer&)
{
    if (bufLen == 0)
    {
        inputBuf[0] = '-';
        inputBuf[1] = '\0';
        bufLen = 1;
    }
    else if (inputBuf[0] == '-')
    {
        memmove(inputBuf, inputBuf + 1, bufLen);
        bufLen--;
    }
    else
    {
        if (bufLen < MAX_BUF - 1)
        {
            memmove(inputBuf + 1, inputBuf, bufLen + 1);
            inputBuf[0] = '-';
            bufLen++;
        }
    }
    notifyChanged();
}

void KeyBoard::onDot(const touchgfx::AbstractButtonContainer&)
{
    for (int i = 0; i < bufLen; i++)
    {
        if (inputBuf[i] == '.')
        {
            return;
        }
    }
    appendChar('.');
}

void KeyBoard::onEnter(const touchgfx::AbstractButtonContainer&)
{
    if (enterCallback != 0 && enterCallback->isValid())
    {
        enterCallback->execute(inputBuf);
    }
}
