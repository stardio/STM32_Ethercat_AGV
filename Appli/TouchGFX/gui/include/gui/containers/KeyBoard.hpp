#ifndef KEYBOARD_HPP
#define KEYBOARD_HPP

#include <gui_generated/containers/KeyBoardBase.hpp>
#include <touchgfx/Callback.hpp>
#include <touchgfx/containers/buttons/AbstractButtonContainer.hpp>

class KeyBoard : public KeyBoardBase
{
public:
    static const int MAX_BUF = 16;

    KeyBoard();
    virtual ~KeyBoard() {}
    virtual void initialize();

    // 부모 화면에서 등록 → Enter 눌렀을 때 최종 문자열 전달
    void setEnterCallback(touchgfx::GenericCallback<const char*>& cb)
    {
        enterCallback = &cb;
    }

    // 부모 화면에서 등록 → 키 입력마다 현재 버퍼 전달 (실시간 표시용)
    void setBufferChangedCallback(touchgfx::GenericCallback<const char*>& cb)
    {
        bufferChangedCallback = &cb;
    }

    // 팝업 열 때 빈 버퍼로 초기화
    void clearBuffer();

    const char* getBuffer() const { return inputBuf; }

protected:
    typedef touchgfx::GenericCallback<const touchgfx::AbstractButtonContainer&> BtnCb;

    char inputBuf[MAX_BUF];
    int  bufLen;

    touchgfx::GenericCallback<const char*>* enterCallback;
    touchgfx::GenericCallback<const char*>* bufferChangedCallback;

    void appendChar(char c);
    void deleteLast();
    void notifyChanged();

    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbN0;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbN1;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbN2;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbN3;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbN4;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbN5;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbN6;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbN7;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbN8;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbN9;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbMinus;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbDot;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbDel;
    touchgfx::Callback<KeyBoard, const touchgfx::AbstractButtonContainer&> cbEnter;

    void onN0(const touchgfx::AbstractButtonContainer&) { appendChar('0'); }
    void onN1(const touchgfx::AbstractButtonContainer&) { appendChar('1'); }
    void onN2(const touchgfx::AbstractButtonContainer&) { appendChar('2'); }
    void onN3(const touchgfx::AbstractButtonContainer&) { appendChar('3'); }
    void onN4(const touchgfx::AbstractButtonContainer&) { appendChar('4'); }
    void onN5(const touchgfx::AbstractButtonContainer&) { appendChar('5'); }
    void onN6(const touchgfx::AbstractButtonContainer&) { appendChar('6'); }
    void onN7(const touchgfx::AbstractButtonContainer&) { appendChar('7'); }
    void onN8(const touchgfx::AbstractButtonContainer&) { appendChar('8'); }
    void onN9(const touchgfx::AbstractButtonContainer&) { appendChar('9'); }
    void onMinus(const touchgfx::AbstractButtonContainer&);
    void onDot(const touchgfx::AbstractButtonContainer&);
    void onDel(const touchgfx::AbstractButtonContainer&) { deleteLast(); }
    void onEnter(const touchgfx::AbstractButtonContainer&);
};

#endif // KEYBOARD_HPP
